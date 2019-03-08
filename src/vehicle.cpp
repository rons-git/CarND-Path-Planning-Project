#include "vehicle.hpp"

int VehiclePlanner::lanePlanner(double s, double d, vector<vector<double>> sensor_fusion) {
  int lane = laneCalc(d);
  int new_lane;
  double distance = closestVehicle(s, lane, sensor_fusion, true)[0];
	// Stay in current lane until deciding to change
  curr_lane = lane; 
	// if adequate space in front, stay in lane and go near the speed limit
  if (distance > 20) {
    new_lane = lane;
    target_vehicle_speed = speed_limit;
		// Reset average costs for laneCost()
    avg_costs = {0,0,0}; 
    return 0;
  } else {
		// Determine new lane based on cost model
    new_lane = laneCost(s, lane, sensor_fusion);
    vector <double> vehicle = closestVehicle(s, new_lane, sensor_fusion, true);
    target_vehicle_speed = vehicle[1];
  }
  // Return New Lane (0 = stay in lane, -4 = change left, 4 = change right)
  if (new_lane == lane) return 0;
  else if (new_lane < lane) return -4;
  else return 4;
}

int VehiclePlanner::laneCalc(double d) {
  // Check which lane the d-value comes from
  // Left is 0, middle is 1, right is 2
  int lane;
  if (d < 4) lane = 0;
  else if (d < 8) lane = 1;
  else lane = 2;
  return lane;
}

vector<double> VehiclePlanner::closestVehicle(double s, int lane, 
	vector<vector<double>> sensor_fusion, bool direction) {
  double dist = 10000;
	// Set to speed limit in case no vehicles in front
  double velocity = 22.352 - 0.5; 
  double vehicle_s;
  double vehicle_d;
  double vehicle_v;
  int vehicle_lane;
  // Check each vehicle in sensor range
  for(int vehicle = 0; vehicle < sensor_fusion.size(); vehicle++) {
    vehicle_s = sensor_fusion[vehicle][5];
    vehicle_d = sensor_fusion[vehicle][6];
    vehicle_v = sqrt(pow(sensor_fusion[vehicle][3], 2) + pow(sensor_fusion[vehicle][4], 2));
    vehicle_lane = laneCalc(vehicle_d);
    if (vehicle_lane == lane) {
      if (direction == true) {
				// Capture distance and speed of vehicle directly in front
        if (vehicle_s > s and (vehicle_s - s) < dist) {
          dist = vehicle_s - s;
          velocity = vehicle_v;
        }
      } else {
				// Capture distance and speed of vehicle directly behind
        if (s >= vehicle_s and (s - vehicle_s) < dist) {
          dist = s - vehicle_s;
          velocity = vehicle_v;
        }
      }
    }
  }
	// Avoid dividing by zero in laneCost()
  if (dist <= 0) dist = 1.0;
  if (lane == curr_lane and direction == true) curr_lead_vehicle_speed = velocity;
  return {dist, velocity};
}

int VehiclePlanner::laneCost(double s, int lane, vector<vector<double>> sensor_fusion) {
  vector <double> costs = {0,0,0};
  vector <double> front_vehicle;
  vector <double> back_vehicle;
  for (int i = 0; i < 3; i++) {
		// Lane Cost
    costs[i] = i * 5;
		// Get closest vehicle ahead and behind distance and speed for each lane
    front_vehicle = closestVehicle(s, i, sensor_fusion, true);
    back_vehicle = closestVehicle(s, i, sensor_fusion, false);
		// Prohibitive cost for vehicle too close
    if (i != lane && (front_vehicle[0] < 10 || back_vehicle[0] < 10)) costs[i] = 15; 
		// Positive cost for slower vehicle in front
		if (front_vehicle[0] < 1000) {
			if (front_vehicle[1] < curr_lead_vehicle_speed) costs[i] += 15;
			if (front_vehicle[1] < speed_limit) costs[i] += 5;
		}
		if (back_vehicle[0] < 1000 && back_vehicle[1] > speed_limit) costs[i] += 15;
    // Simple moving average of costs over the last ten iterations
    avg_costs[i] = (avg_costs[i] * 9) + costs[i];
    avg_costs[i] /= 10;
  }
  // Evaluate potential lane change based on lowest cost
  if (lane == 0) {
    return min_element(avg_costs.begin(), avg_costs.end() - 1) - avg_costs.begin();
  } else if (lane == 1) {
    return min_element(avg_costs.begin(), avg_costs.end())  - avg_costs.begin();
  } else {
    return min_element(avg_costs.begin() + 1, avg_costs.end())  - avg_costs.begin();
  }
}
