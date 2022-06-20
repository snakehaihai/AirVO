#include "ros_publisher.h"

#include <Eigen/Geometry>

RosPublisher::RosPublisher(const RosPublisherConfig& ros_publisher_config): _config(ros_publisher_config){
  if(_config.feature){
    _ros_feature_pub = nh.advertise<sensor_msgs::Image>(_config.feature_topic, 10);
    std::function<void(const FeatureMessgaeConstPtr&)> publish_feature_function = 
        [&](const FeatureMessgaeConstPtr& feature_message){
      cv::Mat drawed_image;
      cv::cvtColor(feature_message->image, drawed_image, cv::COLOR_GRAY2BGR);
      assert(feature_message->inliers.size() == feature_message->keypoints.size());
      for(int i = 0; i < feature_message->inliers.size(); i++){
        if(feature_message->inliers[i]){
          cv::circle(drawed_image, feature_message->keypoints[i].pt, 2, cv::Scalar(0, 255, 0), 1, cv::LINE_AA);        
        }else{
          cv::circle(drawed_image, feature_message->keypoints[i].pt, 2, cv::Scalar(0, 0, 255), 1, cv::LINE_AA);        
        }
      }
      sensor_msgs::ImagePtr ros_feature_msg = cv_bridge::CvImage(std_msgs::Header(), "bgr8", drawed_image).toImageMsg();
      _ros_feature_pub.publish(ros_feature_msg);
    };
    _feature_publisher.Register(publish_feature_function);
    _feature_publisher.Start();
  }

  if(_config.frame_pose){
    _ros_frame_pose_pub = nh.advertise<geometry_msgs::PoseStamped>(_config.frame_pose_topic, 10);
    std::function<void(const FramePoseMessageConstPtr&)> publish_frame_pose_function = 
        [&](const FramePoseMessageConstPtr& frame_pose_message){
      geometry_msgs::PoseStamped pose_stamped;
      pose_stamped.header.stamp = ros::Time::now();
      pose_stamped.header.frame_id = "map";
      pose_stamped.pose.position.x = frame_pose_message->pose(0, 3);
      pose_stamped.pose.position.y = frame_pose_message->pose(1, 3);
      pose_stamped.pose.position.z = frame_pose_message->pose(2, 3);
      Eigen::Quaterniond q(frame_pose_message->pose.block<3, 3>(0, 0));
      pose_stamped.pose.orientation.x = q.x();
      pose_stamped.pose.orientation.y = q.y();
      pose_stamped.pose.orientation.z = q.z();
      pose_stamped.pose.orientation.w = q.w();
      _ros_frame_pose_pub.publish(pose_stamped);
    };
    _frame_pose_publisher.Register(publish_frame_pose_function);
    _frame_pose_publisher.Start();
  }

  if(_config.keyframe){
    _ros_keyframe_pub = nh.advertise<geometry_msgs::PoseArray>(_config.keyframe_topic, 10);
    _ros_keyframe_array.header.stamp = ros::Time::now();
    _ros_keyframe_array.header.frame_id = "map";

    _ros_path_pub = nh.advertise<nav_msgs::Path>(_config.path_topic, 10);
    _ros_path.header.stamp = ros::Time::now();
    _ros_path.header.frame_id = "map";

    std::function<void(const KeyframeMessageConstPtr&)> publish_keyframe_function = 
        [&](const KeyframeMessageConstPtr& keyframe_message){
      std::map<int, int>::iterator it;
      for(int i = 0; i < keyframe_message->ids.size(); i++){
        int keyframe_id = keyframe_message->ids[i];

        geometry_msgs::Pose pose;
        pose.position.x = keyframe_message->poses[i](0, 3);
        pose.position.y = keyframe_message->poses[i](1, 3);
        pose.position.z = keyframe_message->poses[i](2, 3);
        Eigen::Quaterniond q(keyframe_message->poses[i].block<3, 3>(0, 0));
        pose.orientation.x = q.x();
        pose.orientation.y = q.y();
        pose.orientation.z = q.z();
        pose.orientation.w = q.w();

        geometry_msgs::PoseStamped pose_stamped;
        pose_stamped.header.stamp = ros::Time::now();
        pose_stamped.pose = pose;
        
        it = _keyframe_id_to_index.find(keyframe_id);
        if(it == _keyframe_id_to_index.end()){
          _ros_keyframe_array.poses.push_back(pose);
          _ros_path.poses.push_back(pose_stamped);
          _keyframe_id_to_index[keyframe_id] = _ros_keyframe_array.poses.size()-1;
        }else{
          int idx = it->second;
          _ros_keyframe_array.poses[idx] = pose;
          _ros_path.poses[idx] = pose_stamped;
        }
      }
      _ros_keyframe_pub.publish(_ros_keyframe_array);
      _ros_path_pub.publish(_ros_path);
    };
    _keyframe_publisher.Register(publish_keyframe_function);    
    _keyframe_publisher.Start();
  }

  if(_config.map){
    _ros_map_pub = nh.advertise<sensor_msgs::PointCloud> (_config.map_topic, 1);
    _ros_mappoints.header.stamp = ros::Time::now(); 
    _ros_mappoints.header.frame_id = "map"; 

    std::function<void(const MapMessageConstPtr&)> publish_map_function = 
        [&](const MapMessageConstPtr& map_message){
      std::unordered_map<int, int>::iterator it;
      for(int i = 0; i < map_message->ids.size(); i++){
        int mappoint_id = map_message->ids[i];
        it = _mappoint_id_to_index.find(mappoint_id);
        if(it == _mappoint_id_to_index.end()){
          geometry_msgs::Point32 point;
          point.x = map_message->points[i](0);
          point.y = map_message->points[i](1);
          point.z = map_message->points[i](2);
          _ros_mappoints.points.push_back(point);
          _mappoint_id_to_index[mappoint_id] = _ros_mappoints.points.size()-1;
        }else{
          int idx = it->second;
          _ros_mappoints.points[idx].x = map_message->points[i](0);
          _ros_mappoints.points[idx].y = map_message->points[i](1);
          _ros_mappoints.points[idx].z = map_message->points[i](2);
        }
      }
      _ros_map_pub.publish(_ros_mappoints);
    };
    _map_publisher.Register(publish_map_function); 
    _map_publisher.Start();   
  }
}

void RosPublisher::PublishFeature(FeatureMessgaePtr feature_message){
  _feature_publisher.Publish(feature_message);
}

void RosPublisher::PublishFramePose(FramePoseMessagePtr frame_pose_message){
  _frame_pose_publisher.Publish(frame_pose_message);
}

void RosPublisher::PublisheKeyframe(KeyframeMessagePtr keyframe_message){
  _keyframe_publisher.Publish(keyframe_message);
}

void RosPublisher::PublishMap(MapMessagePtr map_message){
  _map_publisher.Publish(map_message);
}

void RosPublisher::ShutDown(){
  _frame_pose_publisher.ShutDown();
  _keyframe_publisher.ShutDown();
  _keyframe_publisher.ShutDown();
  _map_publisher.ShutDown();
}