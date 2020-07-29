/**
 * @file
 * @brief Source file for CVObjectDetection class
 * @author Jonathan Thomas <jonathan@openshot.org>
 *
 * @ref License
 */

/* LICENSE
 *
 * Copyright (c) 2008-2019 OpenShot Studios, LLC
 * <http://www.openshotstudios.com/>. This file is part of
 * OpenShot Library (libopenshot), an open-source project dedicated to
 * delivering high quality video editing and animation solutions to the
 * world. For more information visit <http://www.openshot.org/>.
 *
 * OpenShot Library (libopenshot) is free software: you can redistribute it
 * and/or modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3 of the
 * License, or (at your option) any later version.
 *
 * OpenShot Library (libopenshot) is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with OpenShot Library. If not, see <http://www.gnu.org/licenses/>.
 */

#include "../include/CVObjectDetection.h"

// // Initialize the parameters
// float confThreshold = 0.5; // Confidence threshold
// float nmsThreshold = 0.4;  // Non-maximum suppression threshold
// int inpWidth = 416;  // Width of network's input image
// int inpHeight = 416; // Height of network's input image
// vector<string> classes;

CVObjectDetection::CVObjectDetection(std::string processInfoJson, ProcessingController &processingController)
: processingController(&processingController), processingDevice("CPU"){
    SetJson(processInfoJson);
}

void CVObjectDetection::setProcessingDevice(){
    if(processingDevice == "GPU"){
        net.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
        net.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);
    }
    else if(processingDevice == "CPU"){
        net.setPreferableBackend(cv::dnn::DNN_BACKEND_OPENCV);
        net.setPreferableTarget(cv::dnn::DNN_TARGET_CPU);
    }
}

void CVObjectDetection::detectObjectsClip(openshot::Clip &video, size_t _start, size_t _end, bool process_interval)
{

    start = _start; end = _end;

    video.Open();

    // Load names of classes
    std::ifstream ifs(classesFile.c_str());
    std::string line;
    while (std::getline(ifs, line)) classNames.push_back(line);

    confThreshold = 0.5;
    nmsThreshold = 0.1;

    // Load the network
    if(classesFile == "" || modelConfiguration == "" || modelWeights == "")
        return;
    net = cv::dnn::readNetFromDarknet(modelConfiguration, modelWeights);
    setProcessingDevice();

    size_t frame_number;
    if(!process_interval || end == 0 || end-start <= 0){
        // Get total number of frames in video
        start = video.Start() * video.Reader()->info.fps.ToInt();
        end = video.End() * video.Reader()->info.fps.ToInt();
    }

    for (frame_number = start; frame_number <= end; frame_number++)
    {
         // Stop the feature tracker process
        if(processingController->ShouldStop()){
            return;
        }

        std::shared_ptr<openshot::Frame> f = video.GetFrame(frame_number);
        
        // Grab OpenCV Mat image
        cv::Mat cvimage = f->GetImageCV();

        DetectObjects(cvimage, frame_number);

        // Update progress
        processingController->SetProgress(uint(100*(frame_number-start)/(end-start)));

        std::cout<<"Frame: "<<frame_number<<"\n";
    }
}

void CVObjectDetection::DetectObjects(const cv::Mat &frame, size_t frameId){
    // Get frame as OpenCV Mat 
    cv::Mat blob;

    // Create a 4D blob from the frame.
    int inpWidth, inpHeight;
    inpWidth = inpHeight = 416;

    cv::dnn::blobFromImage(frame, blob, 1/255.0, cv::Size(inpWidth, inpHeight), cv::Scalar(0,0,0), true, false);
    
    //Sets the input to the network
    net.setInput(blob);
    
    // Runs the forward pass to get output of the output layers
    std::vector<cv::Mat> outs;
    net.forward(outs, getOutputsNames(net));
    
    // Remove the bounding boxes with low confidence
    postprocess(frame.size(), outs, frameId);

}


// Remove the bounding boxes with low confidence using non-maxima suppression
void CVObjectDetection::postprocess(const cv::Size &frameDims, const std::vector<cv::Mat>& outs, size_t frameId)
{
    std::vector<int> classIds;
    std::vector<float> confidences;
    std::vector<cv::Rect> boxes;
    
    for (size_t i = 0; i < outs.size(); ++i)
    {
        // Scan through all the bounding boxes output from the network and keep only the
        // ones with high confidence scores. Assign the box's class label as the class
        // with the highest score for the box.
        float* data = (float*)outs[i].data;
        for (int j = 0; j < outs[i].rows; ++j, data += outs[i].cols)
        {
            cv::Mat scores = outs[i].row(j).colRange(5, outs[i].cols);
            cv::Point classIdPoint;
            double confidence;
            // Get the value and location of the maximum score
            cv::minMaxLoc(scores, 0, &confidence, 0, &classIdPoint);
            if (confidence > confThreshold)
            {
                int centerX = (int)(data[0] * frameDims.width);
                int centerY = (int)(data[1] * frameDims.height);
                int width = (int)(data[2] * frameDims.width);
                int height = (int)(data[3] * frameDims.height);
                int left = centerX - width / 2;
                int top = centerY - height / 2;
                
                classIds.push_back(classIdPoint.x);
                confidences.push_back((float)confidence);
                boxes.push_back(cv::Rect(left, top, width, height));
            }
        }
    }
    
    // Perform non maximum suppression to eliminate redundant overlapping boxes with
    // lower confidences
    std::vector<int> indices;
    cv::dnn::NMSBoxes(boxes, confidences, confThreshold, nmsThreshold, indices);

    // std::vector<cv::Rect> sortBoxes;
    // for(auto box : boxes)
    //     sortBoxes.push_back(box);
    // sort.update(sortBoxes, frameId, sqrt(pow(frameDims.width,2) + pow(frameDims.height, 2)));


    // sortBoxes.clear();
    // for(auto TBox : sort.frameTrackingResult)
    //     if(TBox.frame == frameId){
    //         sortBoxes.push_back(TBox.box);
    //     }
    
    // for(int i = 0; i<boxes.size(); i++){
    //     bool found = false;
    //     for(int j = 0; j<sortBoxes.size(); j++){
    //         if( iou(boxes[i], sortBoxes[j]) ){
    //             boxes[i] = sortBoxes[j];
    //             sortBoxes.erase(sortBoxes.begin() + j);
    //             found = true;
    //             break;
    //         }
    //     }
    //     if(!found){
    //         boxes.erase(boxes.begin() + i);
    //         confidences.erase(confidences.begin() + i);
    //         classIds.erase(classIds.begin() + i);
    //     }
    // }
    


    // std::map<int, std::vector<cv::Rect> > rectAndClasses;
    // for(int i=0; i<boxes.size(); i++){
    //     if(rectAndClasses.find(classIds[i]) == rectAndClasses.end()){
    //         std::vector<cv::Rect> bboxes;
    //         rectAndClasses[classIds[i]] = bboxes;
    //     }

    //     rectAndClasses[classIds[i]].push_back(boxes[i]);
    // }

    // for(std::map<int, std::vector<cv::Rect> >::iterator it = rectAndClasses.begin(); it != rectAndClasses.end(); it++){
    //     if(sort.find(it->first) == sort.end()){
    //         SortTracker classTracker;
    //         sort[it->first] = classTracker;
    //     }
    //     sort[it->first].update(it->second, frameId, sqrt(pow(frameDims.width,2) + pow(frameDims.height, 2)));
    // }

    // classIds.clear(); boxes.clear(); confidences.clear();

    // for(std::map<int, SortTracker>::iterator it = sort.begin(); it != sort.end(); it++){
    //     for(auto TBox : it->second.frameTrackingResult){
    //         boxes.push_back(TBox.box);
    //         classIds.push_back(it->first);
    //         confidences.push_back(1);
    //     }
    // }

    
    std::vector<cv::Rect_<float>> normalized_boxes;
    for(auto box : boxes){
        cv::Rect_<float> normalized_box;
        normalized_box.x = (box.x)/(float)frameDims.width;
        normalized_box.y = (box.y)/(float)frameDims.height;
        normalized_box.width = (box.x+box.width)/(float)frameDims.width;
        normalized_box.height = (box.y+box.height)/(float)frameDims.height;
        normalized_boxes.push_back(normalized_box);
    }
    
    detectionsData[frameId] = CVDetectionData(classIds, confidences, normalized_boxes, frameId);
}

bool CVObjectDetection::iou(cv::Rect pred_box, cv::Rect sort_box){
    // determine the (x, y)-coordinates of the intersection rectangle
	int xA = std::max(pred_box.x, sort_box.x);
	int yA = std::max(pred_box.y, sort_box.y);
	int xB = std::min(pred_box.x + pred_box.width, sort_box.x + sort_box.width);
	int yB = std::min(pred_box.y + pred_box.height, sort_box.y + sort_box.height);

	// compute the area of intersection rectangle
	int interArea = std::max(0, xB - xA + 1) * std::max(0, yB - yA + 1);
	// compute the area of both the prediction and ground-truth
	// rectangles
	int boxAArea = (pred_box.width + 1) * (pred_box.height + 1);
	int boxBArea = (sort_box.width + 1) * (sort_box.height + 1);
	// compute the intersection over union by taking the intersection
	// area and dividing it by the sum of prediction + ground-truth
	// areas - the interesection area
	float iou = interArea / (float)(boxAArea + boxBArea - interArea);

    if(iou > 0.75)
        return true;
    return false;
}

// Get the names of the output layers
std::vector<cv::String> CVObjectDetection::getOutputsNames(const cv::dnn::Net& net)
{
    static std::vector<cv::String> names;
    
    //Get the indices of the output layers, i.e. the layers with unconnected outputs
    std::vector<int> outLayers = net.getUnconnectedOutLayers();
    
    //get the names of all the layers in the network
    std::vector<cv::String> layersNames = net.getLayerNames();
    
    // Get the names of the output layers in names
    names.resize(outLayers.size());
    for (size_t i = 0; i < outLayers.size(); ++i)
        names[i] = layersNames[outLayers[i] - 1];
    return names;
}

CVDetectionData CVObjectDetection::GetDetectionData(size_t frameId){
    // Check if the stabilizer info for the requested frame exists
    if ( detectionsData.find(frameId) == detectionsData.end() ) {
        
        return CVDetectionData();
    } else {
        
        return detectionsData[frameId];
    }
}

bool CVObjectDetection::SaveTrackedData(){
    // Create tracker message
    libopenshotobjdetect::ObjDetect objMessage;

    //Save class names in protobuf message
    for(int i = 0; i<classNames.size(); i++){
        std::string* className = objMessage.add_classnames();
        className->assign(classNames.at(i));
    }

    // Iterate over all frames data and save in protobuf message
    for(std::map<size_t,CVDetectionData>::iterator it=detectionsData.begin(); it!=detectionsData.end(); ++it){
        CVDetectionData dData = it->second;
        libopenshotobjdetect::Frame* pbFrameData;
        AddFrameDataToProto(objMessage.add_frame(), dData);
    }

    // Add timestamp
    *objMessage.mutable_last_updated() = TimeUtil::SecondsToTimestamp(time(NULL));

    {
        // Write the new message to disk.
        std::fstream output(protobuf_data_path, ios::out | ios::trunc | ios::binary);
        if (!objMessage.SerializeToOstream(&output)) {
        cerr << "Failed to write protobuf message." << endl;
        return false;
        }
    }

    // Delete all global objects allocated by libprotobuf.
    google::protobuf::ShutdownProtobufLibrary();

    return true;

}

// Add frame object detection into protobuf message.
void CVObjectDetection::AddFrameDataToProto(libopenshotobjdetect::Frame* pbFrameData, CVDetectionData& dData) {

    // Save frame number and rotation
    pbFrameData->set_id(dData.frameId);

    for(size_t i = 0; i < dData.boxes.size(); i++){
        libopenshotobjdetect::Frame_Box* box = pbFrameData->add_bounding_box();

        // Save bounding box data
        box->set_x1(dData.boxes.at(i).x);
        box->set_y1(dData.boxes.at(i).y);
        box->set_x2(dData.boxes.at(i).x + dData.boxes.at(i).width);
        box->set_y2(dData.boxes.at(i).y + dData.boxes.at(i).height);
        box->set_classid(dData.classIds.at(i));
        box->set_confidence(dData.confidences.at(i));

    }
}

// Load JSON string into this object
void CVObjectDetection::SetJson(const std::string value) {
	// Parse JSON string into JSON objects
	try
	{
		const Json::Value root = openshot::stringToJson(value);
		// Set all values that match

		SetJsonValue(root);
	}
	catch (const std::exception& e)
	{
		// Error parsing JSON (or missing keys)
		// throw InvalidJSON("JSON is invalid (missing keys or invalid data types)");
        std::cout<<"JSON is invalid (missing keys or invalid data types)"<<std::endl;
	}
}

// Load Json::Value into this object
void CVObjectDetection::SetJsonValue(const Json::Value root) {

	// Set data from Json (if key is found)
	if (!root["protobuf_data_path"].isNull()){
		protobuf_data_path = (root["protobuf_data_path"].asString());
	}
    if (!root["processing_device"].isNull()){
		processingDevice = (root["processing_device"].asString());
	}
    if (!root["model_configuration"].isNull()){
		modelConfiguration = (root["model_configuration"].asString());
	}
    if (!root["model_weights"].isNull()){
		modelWeights= (root["model_weights"].asString());
	}
    if (!root["classes_file"].isNull()){
		classesFile = (root["classes_file"].asString());
	}
}
