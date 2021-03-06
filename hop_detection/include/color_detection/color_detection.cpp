#include "color_detection/color_detection.h"


namespace hop_detection
{
namespace armor_detectors
{
ColorDetection::ColorDetection(ros::NodeHandle &nh, ros::NodeHandle &pnh, int cam_id):
    nh_ptr_(boost::make_shared<ros::NodeHandle>(nh)),
    pnh_ptr_(boost::make_shared<ros::NodeHandle>(pnh)),
    camera_id_(cam_id),
    name_("armor_detection"),
    //debug_(true),
    //display_(true),
    enemy_color_(EnemyColor::RED),
    dyn_cfg_f_(boost::bind(&ColorDetection::dynCfgCallback, this, _1, _2))
{
    // start dynamic reconfigure 
    dyn_cfg_server_.setCallback(dyn_cfg_f_);
    nh_ptr_ = ros::NodeHandlePtr(new ros::NodeHandle);
    pnh_ptr_ = ros::NodeHandlePtr(new ros::NodeHandle("~"));
    error_info_ = ErrorInfo(hop_detection::common::OK);
    onInit();
}

void ColorDetection::onInit()
{
    nh_ptr_->getParam("cameras/camera_3/camera_matrix", camera_matrix_);
    //std::cout << camera_matrix_[0] << std::endl;
    nh_ptr_->getParam("cameras/camera_3/camera_distortion", camera_distortion_);
    nh_ptr_->getParam(name_ + "/debug", debug_);
    nh_ptr_->getParam(name_ + "/display", display_);
    // get armor params
    double width;
    double height;
    nh_ptr_->getParam("armor_detection/armor_size/width", width);
    //std::cout << width << std::endl;
    nh_ptr_->getParam("armor_detection/armor_size/height", height);
    solveArmorCoordinate(width, height);
    int h;
    int s;
    int v;
    nh_ptr_->getParam("armor_detection/red_range_1/lower/H", h);
    nh_ptr_->getParam("armor_detection/red_range_1/lower/S", s);
    nh_ptr_->getParam("armor_detection/red_range_1/lower/V", v);
    red_range_.push_back(cv::Scalar(h, s, v));
    nh_ptr_->getParam("armor_detection/red_range_1/upper/H", h);
    nh_ptr_->getParam("armor_detection/red_range_1/upper/S", s);
    nh_ptr_->getParam("armor_detection/red_range_1/upper/V", v);
    red_range_.push_back(cv::Scalar(h, s, v));
    nh_ptr_->getParam("armor_detection/red_range_2/lower/H", h);
    nh_ptr_->getParam("armor_detection/red_range_2/lower/S", s);
    nh_ptr_->getParam("armor_detection/red_range_2/lower/V", v);
    red_range_.push_back(cv::Scalar(h, s, v));
    nh_ptr_->getParam("armor_detection/red_range_2/upper/H", h);
    nh_ptr_->getParam("armor_detection/red_range_2/upper/S", s);
    nh_ptr_->getParam("armor_detection/red_range_2/upper/V", v);
    red_range_.push_back(cv::Scalar(h, s, v));
}

void ColorDetection::dynCfgCallback(hop_detection::HopDetectionConfig& config, uint32_t level)
{
	params_.light_max_aspect_ratio = config.light_max_aspect_ratio;
	params_.light_min_aspect_ratio = config.light_min_aspect_ratio;
	params_.light_min_area = config.light_min_area;
	params_.light_max_area = config.light_max_area;
	params_.light_max_angle = config.light_max_angle;
	params_.light_max_angle_diff = config.light_max_angle_diff;
	params_.light_length_ratio = config.light_length_ratio;
	
	params_.armor_max_angle = config.armor_max_angle;
	params_.armor_min_area = config.armor_min_area;
	params_.armor_max_aspect_ratio = config.armor_max_aspect_ratio;
	params_.armor_max_pixel_val = config.armor_max_pixel_val;
	params_.armor_max_stddev = config.armor_max_stddev;
	
	params_.armor_blank_max_angle = config.armor_blank_max_angle;
	params_.armor_blank_min_area = config.armor_blank_min_area;
	params_.blank_light_length_ratio = config.blank_light_length_ratio;
}

bool ColorDetection::updateFrame(const cv::Mat& image_in)
{
    // if (debug_)
    //     TIMER_START(updateFrame);
    if (!image_in.empty()) 
    {
        src_img_ = image_in;
        cv::cvtColor(src_img_, gray_img_, cv::COLOR_BGR2GRAY);
        if (debug_) 
        {
            show_lights_before_filter_ = src_img_.clone();
            show_lights_after_filter_ = src_img_.clone();
            show_armors_befor_filter_ = src_img_.clone();
            show_armors_after_filter_ = src_img_.clone();
            cv::namedWindow( "lights_before_filter", cv::WINDOW_AUTOSIZE );
            cv::namedWindow( "lights_after_filter", cv::WINDOW_AUTOSIZE );
            cv::namedWindow( "armors_befor_filter", cv::WINDOW_AUTOSIZE );
            cv::namedWindow( "armors_after_filter", cv::WINDOW_AUTOSIZE );
            cv::waitKey(1);
            
        }
    } else {
        ROS_DEBUG_NAMED(name_, "Waiting for camera driver...");
        return false;
    }
    // if (debug_)
    //     TIMER_END(updateFrame);
    return true;

}

bool ColorDetection::updateDepth(const cv::Mat& depth_in)
{
    if (!depth_in.empty()) 
    {
        depth_img_ = depth_in;
        depth_updated_ = true;
    } else {
        ROS_DEBUG_NAMED(name_, "Waiting for depth...");
        return false;
    }
    return true;
}

ErrorInfo ColorDetection::detectArmor(double &distance, double &pitch, double &yaw)
{
    //if (debug_)
        TIMER_START(detectArmor);
    
    ROS_DEBUG_NAMED(name_, "Begin to detect armor!");
    
    std::vector<cv::RotatedRect> lights;
    std::vector<ArmorInfo> armors;

    detectLights(src_img_, lights);
    //filterLights(lights);
    possibleArmors(lights, armors);
    //filterArmors(armors);

    if (!armors.empty()) 
    {
        ArmorInfo final_armor = selectFinalArmor(armors);
        calcControlInfo(final_armor, distance, pitch, yaw, 10);
    }
    else
        return ErrorInfo(hop_detection::common::Error);
    lights.clear();
    armors.clear();
    
    if (debug_)
        TIMER_END(detectArmor);

    return ErrorInfo(hop_detection::common::OK);
}

ErrorInfo ColorDetection::detectTarget(double & x_ratio, double& y_ratio, double &area)
{
    //if (debug_)
        TIMER_START(detectArmor);
    
    ROS_DEBUG_NAMED(name_, "Begin to detect armor!");
    
    std::vector<cv::RotatedRect> lights;
    std::vector<ArmorInfo> armors;

    detectLights(src_img_, lights);
    //filterLights(lights);
    possibleArmors(lights, armors);
    //filterArmors(armors);

    if (!armors.empty()) 
    {
        ArmorInfo final_armor = selectFinalArmor(armors);
        calcTargetInfo(final_armor, x_ratio, y_ratio, area);
    }
    else
        return ErrorInfo(hop_detection::common::Error);
    lights.clear();
    armors.clear();
    
    if (debug_)
        TIMER_END(detectArmor);

    return ErrorInfo(hop_detection::common::OK);
}

cv::Mat ColorDetection::extractRed(const cv::Mat &src)
{
    cv::Mat red_hue_upper;
    cv::Mat red_hue_lower;
    cv::cvtColor(src, hsv_img_, cv::COLOR_BGR2HSV_FULL); //CV_BGR2HSV＿FULL covert h to 0-255 instead of 0-180
    cv::inRange(hsv_img_, red_range_[0], red_range_[1], red_hue_lower);
    cv::inRange(hsv_img_, red_range_[2], red_range_[3], red_hue_upper);
    return red_hue_lower | red_hue_upper;
}

void ColorDetection::detectLights(const cv::Mat &src, std::vector<cv::RotatedRect> &lights, int light_color)
{
    cv::Mat binary_light_img;
    // extract color
    if (light_color == EnemyColor::RED)
    {
        binary_light_img = extractRed(src);
    }
    cv::Mat element = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(3, 3));
    cv::dilate(binary_light_img, binary_light_img, element, cv::Point(-1, -1), 1);
    if (debug_)
        cv::imshow("binary_light_img", binary_light_img);

    // find contours
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(binary_light_img, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
    lights.reserve(contours.size());
    for (const auto& contour : contours) 
    {
        cv::RotatedRect single_light = cv::minAreaRect(contour);
        lights.push_back(single_light);
        if (debug_)
        {
            drawRotatedRect(show_lights_before_filter_, single_light, cv::Scalar(50), 2);
        }
    }
    if (debug_) cv::imshow("lights_before_filter", show_lights_before_filter_);
}

void ColorDetection::filterLights(std::vector<cv::RotatedRect> &lights)
{
    std::vector<cv::RotatedRect> rects;
    rects.reserve(lights.size());

    for (const auto &light : lights) {
        double angle = 0.0f;
        double light_aspect_ratio =
            std::max(light.size.width, light.size.height) / std::min(light.size.width, light.size.height);
        angle = light.angle >= 90.0 ? std::abs(light.angle - 90.0) : std::abs(light.angle);

        if (light_aspect_ratio < params_.light_max_aspect_ratio &&
            light_aspect_ratio > params_.light_min_aspect_ratio &&
            angle < params_.light_max_angle && 
            light.size.area() >= params_.light_min_area &&
            light.size.area() <= params_.light_max_area) {
        rects.push_back(light);
        if (debug_)
            drawRotatedRect(show_lights_after_filter_, light, cv::Scalar(50), 2);
        }
    }
    if (debug_)
        cv::imshow("lights_after_filter", show_lights_after_filter_);

    lights = rects;
}

void ColorDetection::possibleArmors(const std::vector<cv::RotatedRect> &lights, std::vector<ArmorInfo> &armors)
{
    for (const auto &light1 : lights) 
    {
        for (const auto &light2 : lights) 
        {
            auto edge1 = std::minmax(light1.size.width, light1.size.height);
            auto edge2 = std::minmax(light2.size.width, light2.size.height);
            auto lights_dis = std::sqrt((light1.center.x - light2.center.x) * (light1.center.x - light2.center.x) +
                (light1.center.y - light2.center.y) * (light1.center.y - light2.center.y));
            auto center_angle =
                std::atan((light1.center.y - light2.center.y)/ (light1.center.x - light2.center.x)) * 180 / CV_PI;

            cv::RotatedRect rect;
            rect.angle = static_cast<float>(center_angle);
            rect.center.x = (light1.center.x + light2.center.x) / 2;
            rect.center.y = (light1.center.y + light2.center.y) / 2;
            float armor_width = static_cast<float>(lights_dis) - std::max(edge1.first, edge2.first);
            float armor_height = std::max<float>(edge1.second, edge2.second);

            rect.size.width = std::max<float>(armor_width, armor_height);
            rect.size.height = std::min<float>(armor_width, armor_height);
			//std::cout << "angle_diff: " << std::abs(light1.angle - light2.angle) << std::endl;
			//td::cout << "center_angle: " << std::abs(center_angle) << std::endl;
			//td::cout << "aspect ratio: " << rect.size.width / (float) (rect.size.height) << std::endl;
			//std::cout <<"area: " << rect.size.area() << std::endl;
            if (std::abs(light1.angle - light2.angle) < params_.light_max_angle_diff &&
                std::abs(center_angle) < params_.armor_max_angle &&
                !std::isnan(center_angle)&&
                rect.size.width / (float) (rect.size.height) < params_.armor_max_aspect_ratio &&
                rect.size.area() > params_.armor_min_area &&
                gray_img_.at<uchar>(static_cast<int>(rect.center.y), static_cast<int>(rect.center.x))
                   < params_.armor_max_pixel_val)   // value entry of hsv, shoudl be low
            {
                std::vector<cv::Point2f> armor_points;
                if (light1.center.x < light2.center.x) {
                    calcArmorInfo(armor_points, light1, light2);
                    
                } else {
                    calcArmorInfo(armor_points, light2, light1);
                }
                
                    for (auto& a:armor_points)
	{
		std::cout <<a.x << a.y<<std::endl;
	}
                armors.emplace_back(ArmorInfo(rect, armor_points));
                std::cout <<"111111" <<std::endl;
                if (debug_)
                    drawRotatedRect(show_armors_befor_filter_, rect, cv::Scalar(100), 2);
                std::cout <<"2222" <<std::endl;
                armor_points.clear();
            }
        }
    }
    if (debug_)
        cv::imshow("armors_before_filter", show_armors_befor_filter_);
}

void ColorDetection::filterArmors(std::vector<ArmorInfo> &armors)
{
    cv::Mat mask = cv::Mat::zeros(src_img_.size(), CV_8UC1);
    for (auto armor_iter = armors.begin(); armor_iter != armors.end();) 
    {
        cv::Point pts[4];
        for (unsigned int i = 0; i < 4; i++) 
        {
            pts[i].x = (int) armor_iter->vertex[i].x;
            pts[i].y = (int) armor_iter->vertex[i].y;
        }
        cv::fillConvexPoly(mask, pts, 4, cv::Scalar(255), 8, 0);

        cv::Mat mat_mean;
        cv::Mat mat_stddev;
        cv::meanStdDev(gray_img_, mat_mean, mat_stddev, mask);

        auto stddev = mat_stddev.at<double>(0, 0);

        if (stddev > params_.armor_max_stddev) 
        {
            armor_iter = armors.erase(armor_iter);
        } 
        else 
        {
            armor_iter++;
        }
    }

    // nms
    std::vector<bool> is_armor(armors.size(), true);
    for (int i = 0; i < armors.size() && is_armor[i] == true; i++) 
    {
        for (int j = i + 1; j < armors.size() && is_armor[j]; j++) 
        {
            float dx = armors[i].rect.center.x - armors[j].rect.center.x;
            float dy = armors[i].rect.center.y - armors[j].rect.center.y;
            float dis = std::sqrt(dx * dx + dy * dy);
            if (dis < armors[i].rect.size.width + armors[j].rect.size.width) 
            {
                if (armors[i].rect.angle > armors[j].rect.angle)
                    is_armor[i] = false;
                else
                    is_armor[j] = false;
            }
        }
    }

    for (auto armor_iter = armors.begin(); armor_iter != armors.end();) 
    {
        if (!is_armor[armor_iter - armors.begin()])
            armor_iter = armors.erase(armor_iter);
        else if (debug_) 
        {
            drawRotatedRect(show_armors_after_filter_, armor_iter->rect, cv::Scalar(255, 255, 255), 2);
            armor_iter++;
        } else
            armor_iter++;
    }
    if (debug_)
        cv::imshow("armors_after_filter", show_armors_after_filter_);
}

ArmorInfo ColorDetection::selectFinalArmor(std::vector<ArmorInfo> &armors)
{
	std::cout << "select" <<std::endl;
    std::sort(armors.begin(),
                armors.end(),
                [](const ArmorInfo &p1, const ArmorInfo &p2) { return p1.rect.size.area() > p2.rect.size.area(); });

    if (debug_) {
        //drawRotatedRect(src_img_, armors[0].rect, cv::Scalar(100, 100, 100), 2);
        //cv::imshow("relust_img_", src_img_);
    }
    std::cout << "select" <<std::endl;
    return armors[0];
}

void ColorDetection::calcTargetInfo(const ArmorInfo & armor, double & x_ratio, double& y_ratio, double &area)
{
	x_ratio = armor.rect.center.x / 640;
	y_ratio = armor.rect.center.y / 480;
	//area = armor.rect.size.area();
	cv::Point2f pts[4];
	std::vector<cv::Point2f> armor_vert;
	armor.rect.points(pts);
	std::sort(pts, pts + 4, [](const cv::Point2f &p1, const cv::Point2f &p2) { return p1.y < p2.y; });
	area = (pts[2].y + pts[3].y) / 2;
	
}

void ColorDetection::calcControlInfo(const ArmorInfo & armor,
                        double &distance,
                        double &pitch,
                        double &yaw,
                        double bullet_speed)
{
	
    cv::Mat rvec;
    cv::Mat tvec;
    std::cout <<armor.vertex.size() <<std::endl;
    for (auto& a:armor.vertex)
	{
		std::cout <<a.x <<" "<< a.y<<std::endl;
	}
	cv::Point2f pts[4];
	std::vector<cv::Point2f> armor_vert;
	armor.rect.points(pts);
	std::sort(pts, pts + 4, [](const cv::Point2f &p1, const cv::Point2f &p2) { return p1.x < p2.x; });
	std::sort(pts, pts + 2, [](const cv::Point2f &p1, const cv::Point2f &p2) { return p1.y < p2.y; });
	std::sort(pts+2, pts + 4, [](const cv::Point2f &p1, const cv::Point2f &p2) { return p1.y < p2.y; });
	    for (auto& a:pts)
	{
		std::cout <<a.x <<" "<< a.y<<std::endl;
		armor_vert.push_back(a);
	}
	    //for (auto& a:armor_points_)
	//{
		//std::cout <<a.x <<" "<< a.y<<std::endl;
		////rmor_vert.push_back(a);
	//}	
    //for (auto& a:camera_matrix_)
	//{
		//a = (double)a;
		//std::cout <<a <<std::endl;
	//}
	
	//for (auto& a:camera_distortion_)
	//{
		//a = (double)a;
		//std::cout <<a <<std::endl;
	//}
	
	cv::Mat_<float> intrinsic(camera_matrix_);
    intrinsic = intrinsic.reshape(3, 3);
    
    
    
    std::cout <<intrinsic << std::endl;
		
    try{
    cv::solvePnP(armor_points_,
                armor_vert,//armor.vertex,//
                intrinsic,
                camera_distortion_,
                rvec,
                tvec);
			}
	catch (cv::Exception& e )
	{const char* err_msg = e.what();
    std::cout << "exception caught: " << err_msg << std::endl;}
	
    double fly_time = tvec.at<double>(2) / 1000.0 / bullet_speed;
    double gravity_offset = 0.5 * 9.8 * fly_time * fly_time * 1000;
    const double gimble_offset = -15;
    double xyz[3] = {tvec.at<double>(0), tvec.at<double>(1) - gravity_offset + gimble_offset, tvec.at<double>(2)};
	std::cout<< "calc control" <<xyz[0] <<" " <<xyz[1] << " " <<xyz[2]<<std::endl;
    //calculate pitch
    pitch = atan(-xyz[1]/xyz[2]);
    //calculate yaw
    yaw = atan2(xyz[0], xyz[2]);

    //radian to angle
    pitch = pitch * 180 / M_PI;
    yaw   = yaw * 180 / M_PI;

    //if (depth_updated_)
    //{
        //distance = (double)(depth_img_.ptr<float> ( (int)armor.rect.center.x )[(int)armor.rect.center.y]) / 1000; 
        //depth_updated_ = false;
    //}
    //else
    distance = sqrt(tvec.at<double>(0)*tvec.at<double>(0) + tvec.at<double>(2)*tvec.at<double>(2));
        
    std::cout<< "calc control " <<distance << " " << pitch << " " << yaw <<std::endl;
}

void ColorDetection::calcArmorInfo(std::vector<cv::Point2f> &armor_points, cv::RotatedRect left_light, cv::RotatedRect right_light)
{
	std::cout<< "calc" << std::endl;
    cv::Point2f left_points[4], right_points[4];
    left_light.points(left_points);
    right_light.points(right_points);
    cv::Point2f right_lu, right_ld, lift_ru, lift_rd;
    std::sort(left_points, left_points + 4, [](const cv::Point2f &p1, const cv::Point2f &p2) { return p1.x < p2.x; });
    std::sort(right_points, right_points + 4, [](const cv::Point2f &p1, const cv::Point2f &p2) { return p1.x < p2.x; });
    if (right_points[0].y < right_points[1].y) {
        right_lu = right_points[0];
        right_ld = right_points[1];
    } else {
        right_lu = right_points[1];
        right_ld = right_points[0];
    }

    if (left_points[2].y < left_points[3].y) {
        lift_ru = left_points[2];
        lift_rd = left_points[3];
    } else {
        lift_ru = left_points[3];
        lift_rd = left_points[2];
    }
    armor_points.push_back(lift_ru);
    armor_points.push_back(right_lu);
    armor_points.push_back(right_ld);
    armor_points.push_back(lift_rd);
    std::cout<< "calc" << std::endl;
}

void ColorDetection::solveArmorCoordinate(const float width, const float height) 
{
    //armor_points_.emplace_back(cv::Point3f(-width/2, height/2,  0.0));
    //armor_points_.emplace_back(cv::Point3f(width/2,  height/2,  0.0));
    //armor_points_.emplace_back(cv::Point3f(width/2,  -height/2, 0.0));
    //armor_points_.emplace_back(cv::Point3f(-width/2, -height/2, 0.0));
    
    //armor_points_.emplace_back(cv::Point3f(-width/2, -height/2, 0.0));
     //armor_points_.emplace_back(cv::Point3f(width/2,  -height/2, 0.0));
     //armor_points_.emplace_back(cv::Point3f(width/2,  height/2,  0.0));
    //armor_points_.emplace_back(cv::Point3f(-width/2, height/2,  0.0));
    
    armor_points_.emplace_back(cv::Point3f(-width/2, -height/2, 0.0));
    armor_points_.emplace_back(cv::Point3f(-width/2, height/2,  0.0));
    
    armor_points_.emplace_back(cv::Point3f(width/2,  -height/2, 0.0));
    armor_points_.emplace_back(cv::Point3f(width/2,  height/2,  0.0));
    
    
    //armor_points_.emplace_back(cv::Point3f(-width/2, height/2,  0.0));
    //armor_points_.emplace_back(cv::Point3f(-width/2, -height/2, 0.0));
    //armor_points_.emplace_back(cv::Point3f(width/2,  -height/2, 0.0));
    //armor_points_.emplace_back(cv::Point3f(width/2,  height/2,  0.0));
    
    
    
}
} // namespace detector
} // namespace hop_detection
