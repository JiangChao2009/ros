#include <iostream>
#include "ros/ros.h"
#include "laser_transform_core.h"
#include "pcl_ros/transforms.h"
#include "sensor_msgs/PointCloud2.h"
#include "sensor_msgs/NavSatFix.h"
#include "sensor_msgs/Imu.h"
#include "ip_connection.h"
#include "brick_imu.h"
#include "bricklet_gps.h"
#include "bricklet_industrial_dual_0_20ma.h"

#define HOST "141.56.161.43"
//#define HOST 	"192.168.0.55"
#define PORT 	4223

#define M_PI	3.14159265358979323846  /* pi */

/*----------------------------------------------------------------------
 * LaserTransform()
 * Constructor
 *--------------------------------------------------------------------*/

LaserTransform::LaserTransform()
{
  publish_new_pcl = false;
  is_imu_connected = false;
  is_gps_connected = false;
  start_latitude = 0;
  start_longitude = 0;

  laser_orientation.setEuler(0,0,-1.57079633);
}

/*----------------------------------------------------------------------
 * ~LaserTransform
 * Destrucot
 *--------------------------------------------------------------------*/

LaserTransform::~LaserTransform()
{
  if (is_imu_connected)
  {
    imu_leds_off(&imu);
    ipcon_destroy(&ipcon);
  }
}

/*----------------------------------------------------------------------
 * Init()
 * Init the TF-Devices
 *--------------------------------------------------------------------*/

int LaserTransform::init()
{
  // create IP connection
  ipcon_create(&ipcon);

  // register connected callback to "cb_connected"
  ipcon_register_callback(&ipcon,
    IPCON_CALLBACK_CONNECTED,
    (void*)connectedCallback,
    this);

  // register enumeration callback to "cb_enumerate"
  ipcon_register_callback(&ipcon,
    IPCON_CALLBACK_ENUMERATE,
    (void*)enumerateCallback,
    this);

  // connect to brickd
  if(ipcon_connect(&ipcon, HOST, PORT) < 0) {
    std::cout << "could not connect to brickd!" << std::endl;
    return false;
  }
  return 0;
}

/*----------------------------------------------------------------------
 * publishMessage()
 * Publish the message.
 *--------------------------------------------------------------------*/

void LaserTransform::publishMessage(ros::Publisher *pub_message)
{
  if (publish_new_pcl)
  {
    ROS_INFO_STREAM("pcloud out");
    pub_message->publish(pcl_out);
    publish_new_pcl = false;
  }
}

/*----------------------------------------------------------------------
 * publishImuMessage()
 * Publish the Imu message.
 *--------------------------------------------------------------------*/

void LaserTransform::publishImuMessage(ros::Publisher *pub_message)
{
  int16_t acc_x, acc_y, acc_z;
  int16_t mag_x, mag_y, mag_z;
  int16_t ang_x, ang_y, ang_z;
  int16_t temp;
  float x = 0.0, y = 0.0, z = 0.0, w = 0.0;
  static uint32_t seq = 0;
  if (is_imu_connected)
  {
    sensor_msgs::Imu imu_msg;

    imu_get_quaternion(&imu, &x, &y, &z, &w);

    imu_get_all_data(&imu, &acc_x, &acc_y, &acc_z, &mag_x, &mag_y,
      &mag_z, &ang_x, &ang_y, &ang_z, &temp);

    // message header
    imu_msg.header.seq = seq;
    imu_msg.header.stamp = ros::Time::now();
    imu_msg.header.frame_id = "world";
    // imu data
    imu_msg.orientation.x = x;
    imu_msg.orientation.y = y;
    imu_msg.orientation.z = z;
    imu_msg.orientation.w = w;

    imu_msg.angular_velocity.x = ang_x;
    imu_msg.angular_velocity.y = ang_y;
    imu_msg.angular_velocity.z = ang_z;

    imu_msg.linear_acceleration.x = acc_x;
    imu_msg.linear_acceleration.y = acc_y;
    imu_msg.linear_acceleration.z = acc_z;

    pub_message->publish(imu_msg);

    seq++;
  }
}

/*----------------------------------------------------------------------
 * publishNavSatFixMessage()
 * Publish the NavSatFix message.
 *--------------------------------------------------------------------*/

void LaserTransform::publishNavSatFixMessage(ros::Publisher *pub_message)
{
  static uint32_t seq = 0;
  uint8_t fix, satellites_view, satellites_used;
  uint16_t pdop, hdop, vdop, epe;
  uint32_t latitude, longitude;
  uint32_t altitude, geoidal_separation;
  char ns, ew;
  if (is_gps_connected)
  {
    // get gps sensor status
    gps_get_status(&gps, &fix, &satellites_view, &satellites_used);

    if (fix == GPS_FIX_NO_FIX)
      return; // No valid data

    gps_get_coordinates(&gps, &latitude, &ns, &longitude, &ew, &pdop,
      &hdop, &vdop, &epe);
    gps_get_altitude(&gps, &altitude, &geoidal_separation);

    sensor_msgs::NavSatFix gps_msg;

    // message header
    gps_msg.header.seq =  seq;
    gps_msg.header.stamp = ros::Time::now();
    gps_msg.header.frame_id = "world";
    // gps status
    gps_msg.status.status = gps_msg.status.STATUS_SBAS_FIX;
    gps_msg.status.service = gps_msg.status.SERVICE_GPS;

    gps_msg.latitude = latitude/1000000.0;
    gps_msg.longitude = longitude/1000000.0;
    gps_msg.altitude = altitude;
    gps_msg.position_covariance_type = gps_msg.COVARIANCE_TYPE_UNKNOWN;

    pub_message->publish(gps_msg);

    seq++;
  }
}

/*----------------------------------------------------------------------
 * messageCallback()
 * Callback function for subscriber.
 *--------------------------------------------------------------------*/

void LaserTransform::messageCallback(const sensor_msgs::PointCloud2::ConstPtr& msg)
{
  //ROS_INFO_STREAM("pcloud in");
  float xp, yp, zp;
  tf::Transform transform;
  getPosition(&xp, &yp, &zp);
  transform.setOrigin( tf::Vector3(xp, yp, zp) );
  tf::Quaternion q = getQuaternion();
  //tf::Quaternion q2;
  //q2.setEulerZYX(0,0,-1.57079633);
  //transform.setRotation(q2);
  transform.setRotation(laser_orientation);
  sensor_msgs::PointCloud2 pcl_tmp;
  pcl_ros::transformPointCloud("/world2", transform,  *msg, pcl_tmp); 
  transform.setRotation(q);
  pcl_ros::transformPointCloud("/world", transform,  pcl_tmp, pcl_out); 
  publish_new_pcl = true;
}

/*----------------------------------------------------------------------
 * connectedCallback()
 * Callback function for Tinkerforge ip connected 
 *--------------------------------------------------------------------*/

void LaserTransform::connectedCallback(uint8_t connect_reason, void *user_data) 
{
  LaserTransform *lt = (LaserTransform*) user_data;
  if (lt->is_imu_connected == false)
    ipcon_enumerate(&(lt->ipcon));
  return;
}

/*----------------------------------------------------------------------
 * enumerateCallback()
 * Callback function for Tinkerforge enumerate
 *--------------------------------------------------------------------*/

void LaserTransform::enumerateCallback(const char *uid, const char *connected_uid,
                  char position, uint8_t hardware_version[3],
                  uint8_t firmware_version[3], uint16_t device_identifier,
                  uint8_t enumeration_type, void *user_data)
{
  LaserTransform *lt = (LaserTransform*) user_data;

  if(enumeration_type == IPCON_ENUMERATION_TYPE_DISCONNECTED)
  {
    return;
  }

  // check if device is an imu
  if(device_identifier == IMU_DEVICE_IDENTIFIER)
  {
    ROS_INFO_STREAM("found IMU with UID:" << uid);
    // Create IMU device object
    imu_create(&(lt->imu), uid, &(lt->ipcon));
    imu_set_convergence_speed(&(lt->imu),0);
    imu_leds_on(&(lt->imu));
    lt->is_imu_connected = true;
  }
  else if (device_identifier == GPS_DEVICE_IDENTIFIER)
  {
    ROS_INFO_STREAM("found GPS with UID:" << uid);
    // Create GPS device object
    gps_create(&(lt->gps), uid, &(lt->ipcon));
    lt->is_gps_connected = true;
  }
  else if (device_identifier == INDUSTRIAL_DUAL_0_20MA_DEVICE_IDENTIFIER)
  {
    ROS_INFO_STREAM("found ID20MA with UID:" << uid);
    // Create IndustrialDual020mA device object
    industrial_dual_0_20ma_create(&(lt->dual020), uid, &(lt->ipcon));
  }
}

/*----------------------------------------------------------------------
 * getQuaternion()
 * get IMU quaternion
 *--------------------------------------------------------------------*/

tf::Quaternion LaserTransform::getQuaternion()
{
  float x = 0.0, y = 0.0, z = 0.0, w = 0.0;
  
  if (is_imu_connected)
    imu_get_quaternion(&imu, &x, &y, &z, &w);
  
  tf::Quaternion q(x, y*-1, z, w);  
  return q;
}

/*----------------------------------------------------------------------
 * getVelocity()
 * get Velocity
 *--------------------------------------------------------------------*/

int LaserTransform::getVelocity(float *velocity)
{
  *velocity = 0.0;
  return true;
}

/*----------------------------------------------------------------------
 * getPosition()
 * get Position
 *--------------------------------------------------------------------*/
 
int LaserTransform::getPosition(float *x_pos, float *y_pos, float *z_pos)
{
  *x_pos = 0;
  *y_pos = 0;
  *z_pos = 0;
  return true;
}
