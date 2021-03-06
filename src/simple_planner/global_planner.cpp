/****************************************************************************
 *  Copyright (C) 2019 RoboMaster.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of 
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program. If not, see <http://www.gnu.org/licenses/>.
 ***************************************************************************/

#include <tf/tf.h>
#include <tf/transform_listener.h>

#include <nav_msgs/Path.h>
#include <geometry_msgs/PoseWithCovarianceStamped.h>
#include <visualization_msgs/Marker.h>

#include <Eigen/Eigen>
#include <chrono>
#include <queue>
#include <cmath>
#include "utility.h"
#include <bitset>

int mat[] = {
    1, 2, 3, 4, 5, 6,
    7, 8, 9, 10, 11, 12,
    13, 14, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 24,
    25, 26, 27, 28, 29, 30,
    31, 32, 33, 34, 35, 36};
int mat2[] = {
    1, 2, 3, 4, 5, 6,
    25, 26, 27, 28, 29, 30,
    13, 14, 15, 16, 17, 18,
    19, 20, 21, 22, 23, 24,
    7, 8, 9, 10, 11, 12,
    31, 32, 33, 34, 35, 36};

namespace robomaster
{
struct State
{
  std::bitset<64> visited;
  float x = 0, y = 0;
  float orientation = 0;
  float g = 0;
  std::vector<int> path;
  friend bool operator<(const State &lhs, const State &rhs)
  {
    // TODO here
    return -lhs.g + lhs.path.size() * 10 < -rhs.g + rhs.path.size() * 10;
  }
};
class Graph
{
  int *id1_, *id2_;

public:
  Graph(int *id1, int *id2)
  {
    id1_ = id1;
    id2_ = id2;
  }
  float p2(float x)
  {
    return x * x;
  }
  float dist(float x1, float y1, float x2, float y2)
  {
    return sqrt(p2(x1 - x2) + p2(y1 - y2));
  }

  float distance_in_degree(float alpha, float beta)
  {
    float phi = abs(beta - alpha);
    while (phi > 360)
      phi -= 360;
    float distance = phi > 180 ? 360 - phi : phi;
    return distance;
  }

  State calc(State start)
  {
    std::priority_queue<State> que;
    que.push(start);
    State best;
    best.g = 1e6;
    int cnt = 0;
    while (que.size())
    {
      const State s = que.top();
      que.pop();

      if (s.path.size() >= 36)
      {
        if (s.g < best.g)
        {
          best = s;
        }
        if (++cnt > 100)
          return best;
      }

      for (size_t i = 1; i <= 72; i++)
      {
        if (!s.visited[(i - 1) % 36 + 1])
        {
          State s2;
          s2.visited = s.visited;
          s2.visited[(i - 1) % 36 + 1] = 1;
          float x1, y1;
          lookup((i - 1) % 36 + 1, i <= 36 ? 1 : 2, x1, y1);     // original location
          lookup((i - 1) % 36 + 1, i <= 36 ? 2 : 1, s2.x, s2.y); // teleport
          // TODO test this part
          float degree = atan2f(y1 - s.y, x1 - s.x) * 180 / M_PI;
          s2.orientation = atan2f(y1 - s2.y, x1 - s2.y) * 180 / M_PI;
          s2.g = s.g + dist(x1, y1, s.x, s.y) + distance_in_degree(s.orientation, degree) / 360.0 * 10 + distance_in_degree(degree, s2.orientation) / 360.0 * 10;
          s2.path = s.path;
          s2.path.push_back(i);
          que.push(s2);
        }
      }
    }
    return best;
  }

  // get the number of a point from row, col and type
  int get_id(int row, int col, int type)
  {
    if (type == 1)
    {
      return id1_[(row - 1) * 6 + col];
    }
    else // if (type == 2)
    {
      return id2_[(row - 1) * 6 + col];
    }
    // return 0;
  }

  // get the location from id and type
  void lookup(int id, int type, float &x, float &y)
  {
    int ans = 0;
    for (size_t i = 0; i < 36; i++)
    {
      if (type == 1)
      {
        if (id1_[i] == id)
        {
          ans = i;
          break;
        }
      }
      else
      {
        if (id2_[i] == id)
        {
          ans = i;
          break;
        }
      }
    }
    int row = ans / 6;
    int col = ans % 6;

    y = row + (type == 1 ? 0.25 : 0.75);
    x = col + (type == 1 ? 0.25 : 0.75);
  }
};

class GlobalPlanner
{
public:
  GlobalPlanner(ros::NodeHandle &given_nh) : nh(given_nh)
  {

    nh.param<int>("plan_frequency", plan_freq_, 50);
    nh.param<std::string>("global_frame", global_frame_, "map");

    // -------------------visulize endpoints and trajectory---------------------
    tf_listener_ = std::make_shared<tf::TransformListener>();

    setpoint_pub_ = nh.advertise<visualization_msgs::Marker>("set_point", 100);
    global_path_pub_ = nh.advertise<nav_msgs::Path>("path", 100);

    init_map();

    Plan();
  }
  ~GlobalPlanner() = default;

private:
  void Plan()
  {
    Graph graph(mat, mat2);
    State best = graph.calc(State());
    nav_msgs::Path path;

    path.header.stamp = ros::Time::now();
    path.header.frame_id = global_frame_;
    for (size_t i = 0; i < 36; i++)
    {

      {
        geometry_msgs::PoseStamped pose;
        pose.header.stamp = ros::Time::now();
        pose.header.frame_id = global_frame_;
        float x, y;
        graph.lookup((best.path[i] - 1) % 36 + 1, best.path[i] <= 36 ? 1 : 2, x, y);
        pose.pose.position.x = x, pose.pose.position.y = y;
        path.poses.push_back(pose);
      }

      {
        geometry_msgs::PoseStamped pose;
        pose.header.stamp = ros::Time::now();
        pose.header.frame_id = global_frame_;
        float x, y;
        graph.lookup((best.path[i] - 1) % 36 + 1, best.path[i] > 36 ? 1 : 2, x, y);
        pose.pose.position.x = x, pose.pose.position.y = y;
        path.poses.push_back(pose);
      }
    }

    global_path_pub_.publish(path);
  }

  void init_map()
  {
    // FIXME the first a few markers does not show
    for (size_t i = 0; i < 6; i++)
    {
      for (size_t j = 0; j < 6; j++)
      {
        mark_point(i + 0.25, j + 0.25, 0, visualization_msgs::Marker::SPHERE);
        mark_point(i + 0.75, j + 0.75, 0, visualization_msgs::Marker::CUBE);
      }
    }
  }

  void mark_point(float x, float y, float z, int shape)
  {
    visualization_msgs::Marker p;
    p.header.frame_id = global_frame_;
    p.header.stamp = ros::Time::now();
    p.id = rand();

    p.type = shape;
    p.action = visualization_msgs::Marker::ADD;

    p.pose.position.x = x;
    p.pose.position.y = y;
    p.pose.position.z = z;
    p.pose.orientation.w = 1;
    p.pose.orientation.x = 0;
    p.pose.orientation.y = 0;
    p.pose.orientation.z = 0;

    p.scale.x = p.scale.y = p.scale.z = 0.1;

    p.color.a = p.color.r = 1.0;
    p.color.g = p.color.b = 0.0;

    p.lifetime = ros::Duration(2000.0);

    setpoint_pub_.publish(p);
    ros::Duration(0.002).sleep();
  }

private:
  ros::NodeHandle nh;
  std::shared_ptr<tf::TransformListener> tf_listener_;
  std::string global_frame_;
  // ros::Timer plan_timer_;

  ros::Publisher global_path_pub_;
  ros::Publisher setpoint_pub_;

  ros::Subscriber waypoint_sub_;
  ros::Subscriber record_sub_;

  ros::Timer record_timer_;

  int point_num_;
  int plan_freq_;
};
} // namespace robomaster

using namespace robomaster;
int main(int argc, char **argv)
{
  ros::init(argc, argv, "global_planner");
  ros::NodeHandle nh("~");
  // ros::Duration(5).sleep();
  GlobalPlanner global_planner(nh);
  ros::spin();
  return 0;
}
