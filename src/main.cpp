#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "helpers.h"
#include "json.hpp"
#include "spline.h"
#include "vehicle.cpp"

using namespace std;
VehiclePlanner vp;

// for convenience
using json = nlohmann::json;

int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }

  h.onMessage([&map_waypoints_x, &map_waypoints_y, &map_waypoints_s,
							&map_waypoints_dx, &map_waypoints_dy](uWS::WebSocket<uWS::SERVER> ws, 
								char *data, size_t length, uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
        	// Main car's localization Data
          	double car_x = j[1]["x"];
          	double car_y = j[1]["y"];
          	double car_s = j[1]["s"];
          	double car_d = j[1]["d"];
          	double car_yaw = j[1]["yaw"];
          	double car_speed = j[1]["speed"];

          	// Previous path data given to the Planner
          	auto previous_path_x = j[1]["previous_path_x"];
          	auto previous_path_y = j[1]["previous_path_y"];
          	// Previous path's end s and d values 
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
          	auto sensor_fusion = j[1]["sensor_fusion"];

          	json msgJson;

            vector<double> next_x_vals;
						vector<double> next_y_vals;

          	// Define a path made up of (x,y) points that the car will visit
						// sequentially every .02 second
						//
            // Start by using remaining previous path
						int path_size = previous_path_x.size();
            for(int i = 0; i < path_size; i++){
              next_x_vals.push_back(previous_path_x[i]);
              next_y_vals.push_back(previous_path_y[i]);
            }
            vector<double> ptsx;
            vector<double> ptsy;
            double ref_x = car_x;
            double ref_y = car_y;
            double ref_yaw = deg2rad(car_yaw);
            double ref_vel;
            // If no previous path, use current simulator values
            if(path_size < 2){
              double prev_car_x = car_x - cos(car_yaw);
              double prev_car_y = car_y - sin(car_yaw);
              ptsx.push_back(prev_car_x);
              ptsx.push_back(car_x);
              ptsy.push_back(prev_car_y);
              ptsy.push_back(car_y);
              ref_vel = car_speed;
            } else {
							// If previous path, use it and calculate angle based on change in x & y position
              ref_x = previous_path_x[path_size - 1];
              ref_y = previous_path_y[path_size - 1];
              double ref_x_prev = previous_path_x[path_size - 2];
              double ref_y_prev = previous_path_y[path_size - 2];
              ref_yaw = atan2(ref_y - ref_y_prev, ref_x - ref_x_prev);
              ref_vel = vp.target_vehicle_speed;
              // Append starter points for spline
              ptsx.push_back(ref_x_prev);
              ptsx.push_back(ref_x); 
              ptsy.push_back(ref_y_prev);
              ptsy.push_back(ref_y);
            }
            // Plan the rest of the path
            vector<double> frenet_vec = getFrenet(
							ref_x, ref_y, ref_yaw, map_waypoints_x, map_waypoints_y);
            double move = vp.lanePlanner(frenet_vec[0], frenet_vec[1], sensor_fusion);
            double lane = vp.curr_lane;
            double next_d = (lane * 4) + 2 + move;
            // Make sure the chosen lane is not blocked
            int check_lane = vp.laneCalc(next_d);
            vector<double> front_vehicle = vp.closestVehicle(
							frenet_vec[0], check_lane, sensor_fusion, true);
            vector<double> back_vehicle = vp.closestVehicle(
							frenet_vec[0], check_lane, sensor_fusion, false);
            // If not enough room to change lanes, stay in lane at leading vehicle speed 
            if (front_vehicle[0] < 10 or back_vehicle[0] < 10 or 
							vp.avg_costs[check_lane] <= -5) {
              next_d = (lane * 4) + 2;
              if (check_lane != lane) vp.target_vehicle_speed = vp.curr_lead_vehicle_speed;
            }
						int horizon = 40;
            // Set further waypoints based on going further along highway in desired lane
            vector <double> wp1 = getXY(car_s + horizon, next_d, map_waypoints_s, 
																				map_waypoints_x, map_waypoints_y);
            vector <double> wp2 = getXY(car_s + horizon * 2, next_d, map_waypoints_s, 
																				map_waypoints_x, map_waypoints_y);
            vector <double> wp3 = getXY(car_s + horizon * 3, next_d, map_waypoints_s, 
																	map_waypoints_x, map_waypoints_y);
            ptsx.push_back(wp1[0]);
            ptsx.push_back(wp2[0]);
            ptsx.push_back(wp3[0]);
            ptsy.push_back(wp1[1]);
            ptsy.push_back(wp2[1]);
            ptsy.push_back(wp3[1]);
            if (ptsx.size() > 2) {  // Spline fails if not greater than two points
              // Shift and rotate points to local coordinates
              for (int i = 0; i < ptsx.size(); i++) {
                double shift_x = ptsx[i] - ref_x;
                double shift_y = ptsy[i] - ref_y;
                ptsx[i] = (shift_x * cos(0 - ref_yaw) - shift_y * sin(0 - ref_yaw));
                ptsy[i] = (shift_x * sin(0 - ref_yaw) + shift_y * cos(0 - ref_yaw));
              }
              // create a spline
              tk::spline s;
              // set (x,y) points to the spline
              s.set_points(ptsx, ptsy);
							// initialize target values
              double target_x = 30;
              double target_y = s(target_x);
              double target_dist = sqrt(pow(target_x, 2) + pow(target_y, 2));
              double x_add_on = 0;
              const int MAX_ACCEL= 10; // meters/second/second
							// Limit acceleration to prevent jerk
              const double accel = MAX_ACCEL * 0.02 * 0.8;
              for(int i = 0; i < horizon - path_size; i++) {
								// Accelerate if under target speed
                if (ref_vel < vp.target_vehicle_speed - accel) {
                  ref_vel += accel;
								// Brake if over target speed
                } else if (ref_vel > vp.target_vehicle_speed + accel) { 
                  ref_vel -= accel;
                }
                // Calculate points along new path
                double N = (target_dist / (.02 * ref_vel));
                double x_point = x_add_on + (target_x) / N;
                double y_point = s(x_point);
                x_add_on = x_point;
                double x_ref = x_point;
                double y_ref = y_point;
                // Rotate and shift back to x and y coordinates
                x_point = (x_ref * cos(ref_yaw) - y_ref * sin(ref_yaw));
                y_point = (x_ref * sin(ref_yaw) + y_ref * cos(ref_yaw));
                x_point += ref_x;
                y_point += ref_y;
                next_x_vals.push_back(x_point);
                next_y_vals.push_back(y_point);
              }
            }
						// Save the end speed to be used for the next frame
            vp.target_vehicle_speed = ref_vel;
						//
						// *** End of path definition ***

            msgJson["next_x"] = next_x_vals;
          	msgJson["next_y"] = next_y_vals;

          	auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
















































































