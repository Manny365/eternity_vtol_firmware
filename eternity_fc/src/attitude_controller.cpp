#include <attitude_controller.h>
#include <iostream>
#include <vector>

#define deltatime 0.005f

void attitude_controller::init(ros::NodeHandle &nh)
{
    init_sub(nh);

    mixer_pub = nh.advertise<sensor_msgs::Joy>("mixer", 10);
    angular_setpoints_pub = nh.advertise<eternity_fc::angular_velocity_sp>("/setpoints/angular_velocity_sp",10);

    //init default values
    angular_velocity_sp.throttle = 0;
    angular_velocity_sp.wx = 0;
    angular_velocity_sp.wy = 0;
    angular_velocity_sp.wz = 0;

    attitude_sp.w = 1;
    attitude_sp.x = 0;
    attitude_sp.y = 0;
    attitude_sp.z = 0;
    attitude_sp.head_speed = 0;

    fast_timer = nh.createTimer(ros::Duration(0.005), &attitude_controller::fast_update, this);
    slow_timer = nh.createTimer(ros::Duration(0.1), &attitude_controller::slow_update, this);

    update_params(nh);

    if (UsingUnreal) {
        ROS_INFO("Using Unreal!!! Will convert coordinate system");
    }
}
void attitude_controller::init_sub(ros::NodeHandle &nh)
{
    odometry_sub = nh.subscribe("/dji_sdk/odometry", 10, &attitude_controller::odometry_callback, this);
    accel_sub = nh.subscribe("/dji_sdk/acceleration", 10, &attitude_controller::accel_sub_callback, this);

    angular_velocity_sp_sub = nh.subscribe("/setpoints/angular_velocity_sp", 10,
                                           &attitude_controller::angular_velocity_sp_callback, this);
    attitude_sp_sub = nh.subscribe("/state_machine/attitude_sp", 10, &attitude_controller::attitude_sp_callback, this);
    mode_sub = nh.subscribe("/state_machine/fc_mode", 10, &attitude_controller::mode_sub_callback, this);

    hover_attitude_sub = nh.subscribe("/eternity_setpoints/hover_attitude_sp", 10,
                                      &attitude_controller::hover_attitude_sub_callback, this);
    dji_attitude_sub = nh.subscribe("/dji_sdk/attitude_quaternion", 10, &attitude_controller::dji_attitude_callback,
                                    this);
}

void attitude_controller::slow_update(const ros::TimerEvent &timerEvent)
{
    update_params(nh);
}

void attitude_controller::fast_update(const ros::TimerEvent &timerEvent)
{
    run_controller(timerEvent);

    static int count = 0;
    if (count++ % 10 == 0) {
    }
}

void attitude_controller::run_controller(const ros::TimerEvent &timerEvent)
{
    static int count = 0;
    count ++;

    switch (mode) {
        case controller_mode::manual: {
            run_manual_controller(deltatime, angular_velocity_sp);
            break;
        }
        case controller_mode::attitude: {
            run_attitude_controller(deltatime, attitude_sp);
            break;
        }
        case controller_mode::hover_attitude: {
            run_hover_attitude_controller(deltatime, hover_attitude_sp);
            break;
        }
        default:
            Aileron = 0;
            Elevator = 0;
            Thrust = 0;
            Rudder = 0;
    }
    sensor_msgs::Joy joy;
    std::vector<float> axes(16);
    joy.axes = axes;
    joy.axes[0] = Aileron;
    joy.axes[1] = Elevator;
    joy.axes[2] = Thrust;
    joy.axes[3] = Rudder;
    mixer_pub.publish(joy);

}

void attitude_controller::run_manual_controller(float DelatTime, eternity_fc::angular_velocity_sp angular_velocity_sp1)
{

//            Aileron = -angular_velocity_sp.wz - angular_velocity_p.x() * angular_velocity.x();
//            Elevator = angular_velocity_sp.wy - angular_velocity_p.y() * angular_velocity.y();
//            Rudder = -angular_velocity_sp.wx - angular_velocity_p.z() * angular_velocity.z();
    Vector3f vel_sp;
    //This map is for hover mode
    vel_sp.x() = -angular_velocity_sp1.wz;
    vel_sp.y() = -angular_velocity_sp1.wy;
    vel_sp.z() = -angular_velocity_sp1.wx;
    Vector3f aer = angular_velocity_controller(DelatTime, vel_sp);
    Aileron = aer.x();
    Elevator = aer.y();
    Rudder = aer.z();
    switch (angular_velocity_sp.engine_mode) {
        case engine_modes::engine_lock:
            Thrust = 0;
            break;
        case engine_modes::engine_straight_forward:
            Thrust = (angular_velocity_sp.throttle + 1) / 2;
            break;
        case engine_modes::engine_control_speed:
            break;
    }
}


void attitude_controller::attitude_sp_callback(const eternity_fc::attitude_sp &sp)
{
    this->attitude_sp = sp;
}

void attitude_controller::dji_attitude_callback(const dji_sdk::AttitudeQuaternion &quad)
{
    this->angular_velocity.x() = quad.wx;
    this->angular_velocity.y() = quad.wy;
    this->angular_velocity.z() = quad.wz;
    this->attitude.w() = quad.q0;
    this->attitude.x() = quad.q1;
    this->attitude.y() = quad.q2;
    this->attitude.z() = quad.q3;
}

void attitude_controller::angular_velocity_sp_callback(const eternity_fc::angular_velocity_sp &sp)
{
    if(this->mode == controller_mode::manual)
        this->angular_velocity_sp = sp;
}

void attitude_controller::mode_sub_callback(const std_msgs::Int32 &mode)
{
    this->mode = static_cast<controller_mode>(mode.data);
}

void attitude_controller::accel_sub_callback(const dji_sdk::Acceleration &acc)
{
    this->local_accel.x() = acc.ax;
    this->local_accel.y() = acc.ay;
    this->local_accel.z() = acc.az;
}

void attitude_controller::odometry_callback(const nav_msgs::Odometry &odometry)
{
//    this->odometry = odometry;
    this->angular_velocity.x() = odometry.twist.twist.angular.x;
    this->angular_velocity.y() = odometry.twist.twist.angular.y;
    this->angular_velocity.z() = odometry.twist.twist.angular.z;

    this->attitude.w() = odometry.pose.pose.orientation.w;
    this->attitude.x() = odometry.pose.pose.orientation.x;
    this->attitude.y() = odometry.pose.pose.orientation.y;
    this->attitude.z() = odometry.pose.pose.orientation.z;

    //vel and pos is in NED
    this->ground_velocity.x() = odometry.twist.twist.linear.x;
    this->ground_velocity.y() = odometry.twist.twist.linear.y;
    this->ground_velocity.z() = -odometry.twist.twist.linear.z;

    this->local_velocity = this->attitude.inverse()._transformVector(this->ground_velocity);
}


void attitude_controller::update_params(ros::NodeHandle &nh)
{
    //rad
    nh.param("max_angular_velocity_x", max_angular_velocity.x(), 10.0f);
    //rad
    nh.param("max_angular_velocity_y", max_angular_velocity.y(), 10.0f);
    //rad
    nh.param("max_angular_velocity_z", max_angular_velocity.z(), 10.0f);

    //TODO:Calcaute them
    nh.param("angular_velocity_p_x", angular_velocity_p.x(), 1.0f);
    nh.param("angular_velocity_p_y", angular_velocity_p.y(), 1.0f);
    nh.param("angular_velocity_p_z", angular_velocity_p.z(), 1.0f);

    nh.param("angular_velocity_i_x", angular_velocity_i.x(), 0.0f);
    nh.param("angular_velocity_i_y", angular_velocity_i.y(), 0.0f);
    nh.param("angular_velocity_i_z", angular_velocity_i.z(), 0.0f);

    nh.param("angular_velocity_d_x", angular_velocity_d.x(), 0.0f);
    nh.param("angular_velocity_d_y", angular_velocity_d.y(), 0.0f);
    nh.param("angular_velocity_d_z", angular_velocity_d.z(), 0.0f);

    nh.param("attitude_p_x", attitude_p.x(), 2.0f);
    nh.param("attitude_p_y", attitude_p.y(), 2.0f);
    nh.param("attitude_p_z", attitude_p.z(), 2.0f);

    nh.param("attitude_i_x", attitude_i.x(), 0.0f);
    nh.param("attitude_i_y", attitude_i.y(), 0.0f);
    nh.param("attitude_i_z", attitude_i.z(), 0.0f);


    nh.param("attitude_d_x", attitude_d.x(), 0.00f);
    nh.param("attitude_d_y", attitude_d.y(), 0.00f);
    nh.param("attitude_d_z", attitude_d.z(), 0.00f);


    nh.param("head_velocity_i", head_velocity_i, 0.1f);
    nh.param("head_velocity_p", head_velocity_p, 1.0f);
    nh.param("head_velocity_d", head_velocity_d, 0.05f);

    nh.param("thrust_weight_ratio", thrust_weight_ratio, 2.0f);

    nh.param("using_unreal", UsingUnreal, false);

    nh.param("k_filter_angular",params.k_filter_angular,0.1f);
    nh.param("k_filter_attitude",params.k_filter_attitude,0.1f);

}

void attitude_controller::angle_axis_from_quat(Quaternionf q0, Quaternionf q_sp, Vector3f &axis)
{
    Quaternionf relative = q0.inverse() * q_sp;

    AngleAxisf angle_axis(relative);
    axis = angle_axis.axis();
    float angle = angle_axis.angle();
    int flag = 0;
    if (angle > M_PI) {
        angle = angle - 2 * M_PI;
        //axis =  axis;
        flag = 1;
    }
    else if (angle < -M_PI) {
        angle += 2 * M_PI;
        axis = axis;
        flag = 2;
    }

    static int count = 0;
    if (count++ % 1 == 0) {
        if (flag != 0) {
//			ROS_INFO("%d angle %f axis %f %f %f",flag,angle * 180.0 /M_PI ,axis.x(),axis.y(),axis.z());
        }
    }
    axis = axis * angle;
}

#define MAX_ANGULAR_I 0.8

Vector3f attitude_controller::angular_velocity_controller(float DeltaTime, Vector3f angular_vel_sp)
{
    static Vector3f err_last = Vector3f(0, 0, 0);
    static Vector3f err_angular_int = Vector3f(0, 0, 0);
    static Vector3f err_d = Vector3f(0, 0, 0);


    Vector3f err = angular_vel_sp - angular_velocity;
    //filter
    err = err_last * (1 - params.k_filter_angular) + err * params.k_filter_angular;
    err_d = (err - err_last) / deltatime;
    err_angular_int = err + err_angular_int * deltatime;
    err_last = err;

    if (angular_velocity_i.x() > 1e-2 && err_angular_int.x() * angular_velocity_i.x() > MAX_ANGULAR_I) {
        err_angular_int.x() = MAX_ANGULAR_I / angular_velocity_i.x();
    }

    if (angular_velocity_i.y() > 1e-2 && err_angular_int.y() * angular_velocity_i.y() > MAX_ANGULAR_I) {
        err_angular_int.y() = MAX_ANGULAR_I / angular_velocity_i.y();
//        ROS_INFO("y int :%f",err_angular_int.y());
    }

    if (angular_velocity_i.z() > 1e-2 && err_angular_int.z() * angular_velocity_i.z() > MAX_ANGULAR_I) {
        err_angular_int.z() = MAX_ANGULAR_I / angular_velocity_i.z();
    }

    if (angular_velocity_i.x() > 1e-2 && err_angular_int.x() * angular_velocity_i.x() < -MAX_ANGULAR_I) {
        err_angular_int.x() = -MAX_ANGULAR_I / angular_velocity_i.x();
    }

    if (angular_velocity_i.y() > 1e-2 && err_angular_int.y() * angular_velocity_i.y() < -MAX_ANGULAR_I) {
        err_angular_int.y() = -MAX_ANGULAR_I / angular_velocity_i.y();
//        static int count = 0;
//        ROS_INFO("y int :%f",err_angular_int.y());
    }

    if (angular_velocity_i.z() > 1e-2 && err_angular_int.z() * angular_velocity_i.z() < -MAX_ANGULAR_I) {
        err_angular_int.z() = -MAX_ANGULAR_I / angular_velocity_i.z();
    }



    float Aileron = err.x() * angular_velocity_p.x() + err_d.x() * angular_velocity_d.x() +
              err_angular_int.x() * angular_velocity_i.x();
    float Elevator = err.y() * angular_velocity_p.y() + err_d.y() * angular_velocity_d.y() +
               err_angular_int.y() * angular_velocity_i.y();
    float Rudder = err.z() * angular_velocity_p.z() + err_d.z() * angular_velocity_d.z() +
             err_angular_int.z() * angular_velocity_i.z();
    return Vector3f(Aileron,Elevator,Rudder);

}

Vector3f product(Vector3f a, Vector3f b)
{
    return Vector3f(a.x() * b.x(), a.y() * b.y(), a.z() * b.z());
}


Eigen::Vector3f attitude_controller::so3_attitude_controller(float DeltaTime, Quaternionf attitude_sp,
                                                  Vector3f external_angular_speed)
{
    static int count = 0;
    count ++;
    Vector3f err_so3;
    static Vector3f err_so3_last = Vector3f(0, 0, 0);
    static Vector3f err_so3_integrate = Vector3f(0, 0, 0);

    angle_axis_from_quat(attitude, attitude_sp, err_so3);
    err_so3_integrate = err_so3_integrate + err_so3* DeltaTime ;
    err_so3 = err_so3_last * (1 - params.k_filter_attitude) + err_so3 * params.k_filter_attitude;
    Vector3f err_d = - angular_velocity * params.attitude_d_angular_ratio + (1-params.attitude_d_angular_ratio) *(err_so3_last - err_so3);
    err_so3_last = err_so3;

    if (attitude_i.x() > 1e-2 && err_so3_integrate.x() * attitude_i.x() > max_angular_velocity.x() / 2) {
        err_so3_integrate.x() = max_angular_velocity.x() / 2 / attitude_i.x();
    }

    if (attitude_i.y() > 1e-2 && err_so3_integrate.y() * attitude_i.y() > max_angular_velocity.y() / 2) {
        err_so3_integrate.y() = max_angular_velocity.y() / 2 / attitude_i.y();
    }

    if (attitude_i.z() > 1e-2 && err_so3_integrate.z() * attitude_i.z() > max_angular_velocity.z() / 2) {
        err_so3_integrate.z() = max_angular_velocity.z() / 2 / attitude_i.z();
    }

    if (attitude_i.x() > 1e-2 && err_so3_integrate.x() * attitude_i.x() <- max_angular_velocity.x() / 2) {
        err_so3_integrate.x() = -max_angular_velocity.x() / 2 / attitude_i.x();
    }

    if (attitude_i.y() > 1e-2 && err_so3_integrate.y() * attitude_i.y() <- max_angular_velocity.y()/2 ) {
        err_so3_integrate.y() = -max_angular_velocity.y()/2  / attitude_i.y();

    }

    if (attitude_i.z() > 1e-2 && err_so3_integrate.z() * attitude_i.z() <- max_angular_velocity.z() / 2) {
        err_so3_integrate.z() = -max_angular_velocity.z() / 2 / attitude_i.z();
    }


    return product(err_so3, attitude_p) +  product(attitude_d,err_d) + product(attitude_i,err_so3_integrate) + external_angular_speed;

}

float attitude_controller::head_velocity_control(float DeltaTime, float head_velocity_sp)
{

    //TODO:Solve problem for not hovering
    float head_velocity = local_velocity.x();
    float err = head_velocity_sp - head_velocity;
    float err_d = local_accel.z();

    intt_head_velocity_err = intt_head_velocity_err + err * DeltaTime;
    if (intt_head_velocity_err / head_velocity_i > 5) {
        intt_head_velocity_err = 5 * head_velocity_i;
    }
    if (intt_head_velocity_err / head_velocity_i < -5) {
        intt_head_velocity_err = -5 * head_velocity_i;
    }

    Vector3f Up = this->attitude._transformVector(Vector3f(1, 0, 0));
    float factor = Up.dot(Vector3f(1, 0, 0));
    if (factor < 0.707) {
        //Should be fixed wing Mode,no more thrust need
        factor = 1;
    }
    //calc the vertical acc setpoint
    float acc_sp = err * head_velocity_p + err_d * head_velocity_d + intt_head_velocity_err * head_velocity_i;
    //inverse vertical acc sp to throttle
    if (acc_sp < -9.81) {
        acc_sp = -9.81;
    }
    float acc_sp_with_gra = acc_sp + 9.81 / factor;
    float throttle_acc = acc_sp_with_gra;

    return throttle_acc / 9.81;
    //Thrust from 0->1
}

void attitude_controller::hover_attitude_sub_callback(const eternity_fc::hover_attitude_sp &sp)
{
    this->hover_attitude_sp = sp;

}

void attitude_controller::run_hover_attitude_controller(float DeltaTime, eternity_fc::hover_attitude_sp hover_sp)
{
    //Make up att
    static int count = 0;
    count++;
    Eigen::Quaternionf base_quat(Eigen::AngleAxisf(M_PI / 2, Eigen::Vector3f::UnitY()));
//    Eigen::Quaternionf base_quat(Eigen::AngleAxisf(0, Eigen::Vector3f::UnitY()));
    float roll_sp = hover_sp.roll;
    float pitch_sp = - hover_sp.pitch;
    Vector3f rpy = quat2eulers(attitude * base_quat.inverse()) * 180 / M_PI;
    float yaw_sp = rpy.z();//+ hover_sp.yaw * deltatime;
    Eigen::AngleAxisf rollAngle(roll_sp * M_PI / 180, Eigen::Vector3f::UnitX());
    Eigen::AngleAxisf pitchAngle(pitch_sp * M_PI / 180, Eigen::Vector3f::UnitY());
    Eigen::AngleAxisf yawAngle(yaw_sp * M_PI / 180, Eigen::Vector3f::UnitZ());
    Eigen::Quaternionf quat_sp = yawAngle * pitchAngle * rollAngle * base_quat;

    //set up att sp
    eternity_fc::attitude_sp att_sp;
    att_sp.head_speed = hover_sp.vertical_speed;
    att_sp.w = quat_sp.w();
    att_sp.x = quat_sp.x();
    att_sp.y = quat_sp.y();
    att_sp.z = quat_sp.z();
    att_sp.engine_mode = hover_sp.engine_mode;
    //run controll as att

    Vector3f yaw_angular_speed(0, 0, hover_sp.yaw * M_PI / 180);
    yaw_angular_speed = attitude.inverse()._transformVector(yaw_angular_speed);
//	yaw_angular_speed = attitude._transformVector(yaw_angular_speed);
    run_attitude_controller(deltatime, att_sp, yaw_angular_speed);
    if (count % 10 ==0)
    {
        ROS_INFO("Roll :%f pitch %f yaw %f",rpy.x(),rpy.y(),rpy.z());
    }
}

void attitude_controller::run_attitude_controller(float DeltaTime, eternity_fc::attitude_sp attitude_sp,
                                                  Vector3f external_angular_speed)
{
//    ROS_INFO("Here is the attitude controller");
    static int  count = 0;
    count ++;
    Quaternionf att_sp;
    att_sp.w() = attitude_sp.w;
    att_sp.x() = attitude_sp.x;
    att_sp.y() = attitude_sp.y;
    att_sp.z() = attitude_sp.z;
    Vector3f angular_vel_sp = so3_attitude_controller(deltatime, att_sp, external_angular_speed);
    Vector3f aer = angular_velocity_controller(DeltaTime, angular_vel_sp);

    Aileron = aer.x();
    Elevator = aer.y();
    Rudder = aer.z();

    switch (attitude_sp.engine_mode) {
        case engine_modes::engine_control_speed:
            head_velocity_control(deltatime, attitude_sp.head_speed);
            break;
        case engine_modes::engine_straight_forward:
            Thrust = (attitude_sp.head_speed + 1) / 2;
            break;
        case engine_modes::engine_lock:
            Thrust = 0;
            break;
    }
//    if (count % 10 == 0)
    {
        eternity_fc::angular_velocity_sp angular_velocity_setpoint;
        angular_velocity_setpoint.wx = angular_vel_sp.x();
        angular_velocity_setpoint.wy = angular_vel_sp.y();
        angular_velocity_setpoint.wz = angular_vel_sp.z();
        angular_velocity_setpoint.engine_mode = attitude_sp.engine_mode;
        angular_velocity_setpoint.throttle = attitude_sp.head_speed;
        angular_setpoints_pub.publish(angular_velocity_setpoint);
    }
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "attitude_controller");
    ros::NodeHandle nh("attitude_controller");
    attitude_controller attitude_controller1(nh);
    ROS_INFO("Attitude Controller READY");

    ros::AsyncSpinner spinner(2); // Use 4 threads
    spinner.start();
    ros::waitForShutdown();
    return 0;
}
