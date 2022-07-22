#include "line_processor.h"

#include <math.h>
#include <float.h>
#include <iostream>
#include <numeric>

#include "timer.h"

INITIALIZE_TIMER;

LineDetector::LineDetector(const LineDetectorConfig &line_detector_config): _line_detector_config(line_detector_config){
  fld = cv::ximgproc::createFastLineDetector(line_detector_config.length_threshold, line_detector_config.distance_threshold, 
      // line_detector_config.canny_th1, line_detector_config.canny_th2, line_detector_config.canny_aperture_size, line_detector_config.do_merge);
      line_detector_config.canny_th1, line_detector_config.canny_th2, line_detector_config.canny_aperture_size, false);
}

void LineDetector::LineExtractor(cv::Mat& image, std::vector<Eigen::Vector4d>& lines){
  START_TIMER;
  std::vector<Eigen::Vector4f> source_lines, dst_lines;
  std::vector<cv::Vec4f> cv_lines;
  fld->detect(image, cv_lines);
  for(auto& cv_line : cv_lines){
    source_lines.emplace_back(cv_line[0], cv_line[1], cv_line[2], cv_line[3]);
  }
  STOP_TIMER("Line detect");
  START_TIMER;

  if(_line_detector_config.do_merge){
    MergeLines(source_lines, dst_lines);
    for(auto& line : dst_lines){
      lines.push_back(line.cast<double>());
    }
  }else{
    for(auto& line : source_lines){
      lines.push_back(line.cast<double>());
    }
  }
  STOP_TIMER("Merge");
}

void LineDetector::MergeLines(std::vector<Eigen::Vector4f>& source_lines, std::vector<Eigen::Vector4f>& dst_lines){
  size_t source_line_num = source_lines.size();
  Eigen::Array4Xf line_array = Eigen::Map<Eigen::Array4Xf, Eigen::Unaligned>(source_lines[0].data(), 4, source_lines.size());
  Eigen::ArrayXf x1 = line_array.row(0);
  Eigen::ArrayXf y1 = line_array.row(1);
  Eigen::ArrayXf x2 = line_array.row(2);
  Eigen::ArrayXf y2 = line_array.row(3);

  Eigen::ArrayXf dx = x2 - x1;
  Eigen::ArrayXf dy = y2 - y1;
  Eigen::ArrayXf dot = (x1 * y2 - x2 * y1).abs();
  Eigen::ArrayXf eigen_distances = dot / (dx * dx + dy * dy).sqrt();
  Eigen::ArrayXf eigen_angles = (dy / dx).atan();

  std::vector<float> distances(&eigen_distances[0], eigen_distances.data()+eigen_distances.cols()*eigen_distances.rows());
  std::vector<float> angles(&eigen_angles[0], eigen_angles.data()+eigen_angles.cols()*eigen_angles.rows());

  std::vector<size_t> indices(distances.size());                                                        
  std::iota(indices.begin(), indices.end(), 0);                                                      
  std::sort(indices.begin(), indices.end(), [&distances](size_t i1, size_t i2) { return distances[i1] > distances[i2]; });

  // search clusters
  float theta_upper_thr = cos((10 * M_PI / 180));
  float theta_lower_thr = cos((170 * M_PI / 180));

  float angle_thr = _line_detector_config.angle_thr;
  float distance_thr = _line_detector_config.distance_thr;
  float ep_thr = _line_detector_config.ep_thr * _line_detector_config.ep_thr;
  float quater_PI = M_PI / 4.0;
  std::vector<std::vector<size_t>> cluster_ids;
  std::vector<bool> sort_by_x;
  for(size_t i = 0; i < source_line_num; ){
    std::vector<size_t> cluster;
    size_t idx1 = indices[i];
    cluster.push_back(idx1);
    if(i == source_line_num-1) break;
    float x11 = source_lines[idx1](0);
    float y11 = source_lines[idx1](1);
    float x12 = source_lines[idx1](2);
    float y12 = source_lines[idx1](3);
    float distance1 = distances[idx1];
    float angle1 = angles[idx1];
    bool to_sort_x = (std::abs(angle1) < quater_PI);
    sort_by_x.push_back(to_sort_x);
    if((to_sort_x && (x12 < x11)) || ((!to_sort_x) && y12 < y11)){
      std::swap(x11, x12);
      std::swap(y11, y12);
    }

    for(size_t j = i +1; j < source_line_num; j++){
      size_t idx2 = indices[j];
      float x21 = source_lines[idx2](0);
      float y21 = source_lines[idx2](1);
      float x22 = source_lines[idx2](2);
      float y22 = source_lines[idx2](3);
      float distance2 = distances[idx2];
      float angle2 = angles[idx2];
      if((to_sort_x && (x22 < x21)) || ((!to_sort_x) && y22 < y21)){
        std::swap(x21, x22);
        std::swap(y21, y22);
      }

     
      // check theta
      // std::function<bool(size_t&, size_t&)> CheckCosDelta = [&](size_t& i, size_t& j){
      //   Eigen::Vector2f v21_11 = source_lines[j].head(2) - source_lines[i].head(2);
      //   Eigen::Vector2f v21_12 = source_lines[j].head(2) - source_lines[i].tail(2);
      //   Eigen::Vector2f v22_11 = source_lines[j].tail(2) - source_lines[i].head(2);
      //   Eigen::Vector2f v22_12 = source_lines[j].tail(2) - source_lines[i].tail(2);
      //   float cos_theta1 = (float)(v21_11.transpose() * v21_12) / (float)(v21_11.squaredNorm() * v21_12.squaredNorm());
      //   float cos_theta2 = (float)(v22_11.transpose() * v22_12) / (float)(v22_11.squaredNorm() * v22_12.squaredNorm()); 
      //   return !(cos_theta1 < theta_upper_thr && cos_theta1 > theta_lower_thr) || (cos_theta2 < theta_upper_thr && cos_theta2 > theta_lower_thr);
      // };
      bool to_merge = true;
      Eigen::Vector2f v21_11 = source_lines[idx2].head(2) - source_lines[idx1].head(2);
      Eigen::Vector2f v21_12 = source_lines[idx2].head(2) - source_lines[idx1].tail(2);
      Eigen::Vector2f v22_11 = source_lines[idx2].tail(2) - source_lines[idx1].head(2);
      Eigen::Vector2f v22_12 = source_lines[idx2].tail(2) - source_lines[idx1].tail(2);

      float v21_11_norm = v21_11.norm();
      float v21_12_norm = v21_12.norm();
      float v22_11_norm = v22_11.norm();
      float v22_12_norm = v22_12.norm();

      float cos_theta11 = (float)(v21_11.transpose() * v22_11) / (v21_11_norm * v22_11_norm);
      float cos_theta12 = (float)(v21_12.transpose() * v22_12) / (v21_12_norm * v22_12_norm);
      float cos_theta21 = (float)(v21_11.transpose() * v21_12) / (v21_11_norm * v21_12_norm);
      float cos_theta22 = (float)(v22_11.transpose() * v22_12) / (v22_11_norm * v22_12_norm);

      to_merge = to_merge && (cos_theta11 > theta_upper_thr || cos_theta11 < theta_lower_thr);
      to_merge = to_merge && (cos_theta12 > theta_upper_thr || cos_theta12 < theta_lower_thr);
      to_merge = to_merge && (cos_theta21 > theta_upper_thr || cos_theta21 < theta_lower_thr);
      to_merge = to_merge && (cos_theta22 > theta_upper_thr || cos_theta22 < theta_lower_thr);

      std::cout << "(" << x11 << "," << y11 << "), " << "(" << x12 << "," << y12 << ") --- "
                << "(" << x21 << "," << y21 << "), " << "(" << x22 << "," << y22 << ")" << std::endl;

      std::cout << "theta_upper_thr = " << theta_upper_thr << " theta_lower_thr = " << theta_lower_thr << std::endl;
      std::cout << " cos_theta11 = " << cos_theta11 
                << " cos_theta12 = " << cos_theta12 
                << " cos_theta21 = " << cos_theta21 
                << " cos_theta22 = " << cos_theta22 << std::endl;
      if(to_merge) std::cout << "<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<" << std::endl;


      float d_angle_case1 = std::abs(angle2 - angle1);
      float d_angle_case2 = M_PI + std::min(angle1, angle2) - std::max(angle1, angle2);
      float d_angle = std::min(d_angle_case1, d_angle_case2);
      float d_distance = std::abs(distance2 - distance1);
      if(to_merge){
        // float d_angle_case1 = std::abs(angle2 - angle1);
        // float d_angle_case2 = M_PI + std::min(angle1, angle2) - std::max(angle1, angle2);
        // float d_angle = std::min(d_angle_case1, d_angle_case2);
        // float d_distance = std::abs(distance2 - distance1);
        to_merge = (d_angle < angle_thr && d_distance < distance_thr);
      }

      if(to_merge){ 
        float cx12, cy12, cx21, cy21;
        if((to_sort_x && x12 > x22) || (!to_sort_x && y12 > y22)){
          cx12 = x22;
          cy12 = y22;
          cx21 = x11;
          cy21 = y11;
        }else{
          cx12 = x12;
          cy12 = y12;
          cx21 = x21;
          cy21 = y21;
        }
        to_merge = ((to_sort_x && cx12 >= cx21) || (!to_sort_x && cy12 >= cy21));
        if(!to_merge){
          float d_ep = (cx21 - cx12) * (cx21 - cx12) + (cy21 - cy12) * (cy21 - cy12);
          to_merge = (d_ep < ep_thr);
        }
      }

      std::cout << "to_merge = " << to_merge 
                << " i = " << i << " distance1 = " << distance1 << " angle1 = " << angle1 
                << " j = " << j << " distance2 = " << distance2 << " angle2 = " << angle2 
                << " d_distance = " << d_distance << " d_angle = " << d_angle << std::endl << std::endl << std::endl;

      i = j;
      if(to_merge){
        cluster.push_back(idx2);
      }else{
        break;
      }
    }
    cluster_ids.push_back(cluster);
  }

  // merge clusters
  dst_lines.clear();
  dst_lines.reserve(cluster_ids.size());
  for(size_t i = 0; i < cluster_ids.size(); i++){
    float min_x = DBL_MAX;
    float min_y = DBL_MAX;
    float max_x = -1;
    float max_y = -1;
    std::cout << "new cluster i = " << i << std::endl;
    std::cout << "sort_by_x[i] = " << sort_by_x[i] << std::endl;
    for(auto& idx : cluster_ids[i]){
      float x1 = source_lines[idx](0);
      float y1 = source_lines[idx](1);
      float x2 = source_lines[idx](2);
      float y2 = source_lines[idx](3);

      // if overlap is too big
      {
        float o1, o2, n1, n2;
        if(sort_by_x[i]){
          o1 = min_x;
          o2 = max_x;
          n1 = std::min(x1, x2);
          n2 = std::max(x1, x2);
        }else{
          o1 = min_y;
          o2 = max_y;
          n1 = std::min(y1, y2);
          n2 = std::max(y1, y2);       
        }
        float overlap_length, overlap_rate;
        if((o2 > n2 && n2 > o1 && o1 > n1) || (n2 > o2 && o2 > n1 && n1 >o1)){
          float overlap_length = std::min(o2, n2) - std::max(o1, n1);
          float delta_o = o2 - o1;
          float delta_n = n2 - n1;
          float overlap_rate = overlap_length / std::min(delta_o, delta_n);
          if(overlap_rate > 0.7){
            if(delta_n > delta_o){
              if((sort_by_x[i] && x2 > x1) || (!sort_by_x[i] && y2 > y1)){
                min_x = x1;
                min_y = y1;
                max_x = x2;
                max_y = y2;
              }else{
                min_x = x2;
                min_y = y2;
                max_x = x1;
                max_y = y1;
              }
            }
            continue;
          }
        }
      }

      // std::cout << "x1 = " << x1 << " y1 = " << y1 << " x2 = " << x2 << " y2 = " << y2 << std::endl;
      MinXY(min_x, min_y, x1, y1, sort_by_x[i]);
      // std::cout << "min_x = " << min_x << " min_y = " << min_y << " max_x = " << max_x << " max_y = " << max_y << std::endl;
      MinXY(min_x, min_y, x2, y2, sort_by_x[i]);
      // std::cout << "min_x = " << min_x << " min_y = " << min_y << " max_x = " << max_x << " max_y = " << max_y << std::endl;
      MaxXY(max_x, max_y, x1, y1, sort_by_x[i]);
      // std::cout << "min_x = " << min_x << " min_y = " << min_y << " max_x = " << max_x << " max_y = " << max_y << std::endl;
      MaxXY(max_x, max_y, x2, y2, sort_by_x[i]);
      // std::cout << "min_x = " << min_x << " min_y = " << min_y << " max_x = " << max_x << " max_y = " << max_y << std::endl;
    }
    std::cout << "---------------new cluster end -----------" << std::endl;
    dst_lines.emplace_back(min_x, min_y, max_x, max_y);
  }
}

void LineDetector::MinXY(float& min_x, float& min_y, float& x, float& y, bool to_sort_x){
  if((to_sort_x && min_x > x) || (!to_sort_x && min_y > y)){
    min_x = x;
    min_y = y;
  }
}

void LineDetector::MaxXY(float& max_x, float& max_y, float& x, float& y, bool to_sort_x){
  if((to_sort_x && max_x < x) || (!to_sort_x && max_y < y)){
    max_x = x;
    max_y = y;
  }
}