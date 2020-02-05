#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <vector>

#define TRAFFIC_LIGHT_VO_ID_PREFIX "TL_"

namespace apollo {

namespace common {

class Status {
   public:
    static Status OK() { return Status(); }
};

class SLPoint {
   public:
    double s() const { return 0; }
    double l() const { return 0; }
    void set_s(double) {}
    void set_l(double) {}
};

class Point3D {
   public:
    double x() const { return 0; }
    double y() const { return 0; }
};

class VehicleStateProvider {
   public:
    static VehicleStateProvider* Instance() { return new VehicleStateProvider(); }
    double x() { return 0; }
    double y() { return 0; }
    double linear_velocity() { return 0; }
};

namespace math {
class Vec2d {
   public:
    double x;
    double y;
    Vec2d() {}
    Vec2d(double x, double y) : x(x), y(y) {}
};
}  // namespace math

namespace util {
double DistanceXY(common::math::Vec2d a, common::math::Vec2d b) { return 0; }
}  // namespace util

}  // namespace common

namespace hdmap {

class PathOverlap {
   public:
    double start_s;
    double end_s;
    std::string object_id;
};

class Path {
   public:
    const std::vector<hdmap::PathOverlap> signal_overlaps() const { return std::vector<hdmap::PathOverlap>(); }
};
}  // namespace hdmap

namespace perception {
class TrafficLight {
   public:
    enum TrafficLightColor {
        GREEN = 3
    };
    TrafficLightColor color() { return TrafficLightColor(); }
};
}  // namespace perception

namespace planning {

enum StopReasonCode {
    STOP_REASON_SIGNAL = 100
};

class Frame {
   public:
    apollo::perception::TrafficLight GetSignal(std::string s) { return apollo::perception::TrafficLight(); }
};

class SLBoundery {
   public:
    double start_s() const { return 0; }
    double end_s() const { return 0; }
    double start_l() const { return 0; }
    double end_l() const { return 0; }
};

class STBoundery {
   public:
    bool IsEmpty() const { return true; }
};

class Obstacle {
   public:
    std::string Id() const { return ""; }
    const SLBoundery PerceptionSLBoundary() const { return SLBoundery(); }
    const STBoundery reference_line_st_boundary() const { return STBoundery(); }
};

class PathDecision {
   public:
    std::vector<Obstacle*> obstacles() { return std::vector<Obstacle*>(); }
};

class ReferenceLine {
   public:
    bool IsOnLane(SLBoundery s) const { return true; }
    bool IsOnRoad(SLBoundery s) const { return true; }
    bool XYToSL(const common::math::Vec2d& v, const common::SLPoint* p) const { return true; }
    bool SLToXY(const common::SLPoint& p, const common::math::Vec2d* v) const { return true; }
    hdmap::Path map_path() const { return hdmap::Path(); }
};

class ReferenceLineInfo {
   public:
    PathDecision* path_decision() { return nullptr; }
    SLBoundery AdcSlBoundary() { return SLBoundery(); }
    ReferenceLine reference_line() { return ReferenceLine(); }
};

class TrafficLightStatus {
   public:
    std::vector<std::string> done_traffic_light_overlap_id() const { return std::vector<std::string>(); }
};

class PlanningStatus {
   public:
    TrafficLightStatus traffic_light() { return TrafficLightStatus(); }
};

class PlanningContext {
   public:
    static PlanningContext* Instance() { return new PlanningContext(); }
    PlanningStatus planning_status() {}
};

class TrafficLight_ {
   public:
    double max_stop_deceleration() { return 0; }
    double stop_distance() { return 0; }
};

class TrafficRuleConfig {
   public:
    enum TrafficRuleConfig_RuleId {
        DEFAULT = 1
    };
    TrafficLight_ traffic_light() { return TrafficLight_(); }
    static std::string RuleId_Name(TrafficRuleConfig_RuleId) { return ""; }
    TrafficRuleConfig_RuleId rule_id() { return TrafficRuleConfig_RuleId::DEFAULT; }
};

namespace util {

double FLAGS_max_stop_speed;
double GetADCStopDeceleration(const double adc_front_edge_s,
                              const double stop_line_s) {
    double adc_speed =
        common::VehicleStateProvider::Instance()->linear_velocity();
    if (adc_speed < FLAGS_max_stop_speed) {
        return 0.0;
    }

    double stop_distance = 0;

    if (stop_line_s > adc_front_edge_s) {
        stop_distance = stop_line_s - adc_front_edge_s;
        return (adc_speed * adc_speed) / (2 * stop_distance);
    }
    return std::numeric_limits<double>::max();
}
int BuildStopDecision(const std::string& stop_wall_id, const double stop_line_s,
                      const double stop_distance,
                      const StopReasonCode& stop_reason_code,
                      const std::vector<std::string>& wait_for_obstacles,
                      const std::string& decision_tag, Frame* const frame,
                      ReferenceLineInfo* const reference_line_info) { return 0; }
}  // namespace util

using apollo::common::Status;
using apollo::common::math::Vec2d;
using apollo::hdmap::PathOverlap;

class TrafficLight {
   public:
    TrafficRuleConfig config_;

    Status ApplyRule(Frame* const frame,
                     ReferenceLineInfo* const reference_line_info) {
        MakeDecisions(frame, reference_line_info);

        return Status::OK();
    }

    void MakeDecisions(Frame* const frame,
                       ReferenceLineInfo* const reference_line_info) {
        const auto& traffic_light_status =
            PlanningContext::Instance()->planning_status().traffic_light();

        const double adc_front_edge_s = reference_line_info->AdcSlBoundary().end_s();
        const double adc_back_edge_s = reference_line_info->AdcSlBoundary().start_s();

        const std::vector<PathOverlap>& traffic_light_overlaps =
            reference_line_info->reference_line().map_path().signal_overlaps();
        for (const auto& traffic_light_overlap : traffic_light_overlaps) {
            if (traffic_light_overlap.end_s <= adc_back_edge_s) {
                continue;
            }

            // check if traffic-light-stop already finished, set by scenario/stage
            bool traffic_light_done = false;
            for (auto done_traffic_light_overlap_id :
                 traffic_light_status.done_traffic_light_overlap_id()) {
                if (traffic_light_overlap.object_id == done_traffic_light_overlap_id) {
                    traffic_light_done = true;
                    break;
                }
            }
            if (traffic_light_done) {
                continue;
            }

            // work around incorrect s-projection along round routing
            constexpr double kSDiscrepanceTolerance = 10.0;
            const auto& reference_line = reference_line_info->reference_line();
            common::SLPoint traffic_light_sl;
            traffic_light_sl.set_s(traffic_light_overlap.start_s);
            traffic_light_sl.set_l(0);
            common::math::Vec2d traffic_light_point;
            reference_line.SLToXY(traffic_light_sl, &traffic_light_point);
            common::math::Vec2d adc_position = {
                common::VehicleStateProvider::Instance()->x(),
                common::VehicleStateProvider::Instance()->y()};
            const double distance =
                common::util::DistanceXY(traffic_light_point, adc_position);
            const double s_distance = traffic_light_overlap.start_s - adc_front_edge_s;
            if (s_distance >= 0 &&
                fabs(s_distance - distance) > kSDiscrepanceTolerance) {
                continue;
            }

            auto signal_color =
                frame->GetSignal(traffic_light_overlap.object_id).color();
            const double stop_deceleration = util::GetADCStopDeceleration(
                adc_front_edge_s, traffic_light_overlap.start_s);

            if (signal_color == perception::TrafficLight::GREEN) {
                continue;
            } else {
                // Red/Yellow/Unown: check deceleration
                if (stop_deceleration > config_.traffic_light().max_stop_deceleration()) {
                    continue;
                }
            }

            // build stop decision
            std::string virtual_obstacle_id =
                TRAFFIC_LIGHT_VO_ID_PREFIX + traffic_light_overlap.object_id;
            const std::vector<std::string> wait_for_obstacles;
            util::BuildStopDecision(virtual_obstacle_id,
                                    traffic_light_overlap.start_s,
                                    config_.traffic_light().stop_distance(),
                                    StopReasonCode::STOP_REASON_SIGNAL,
                                    wait_for_obstacles,
                                    TrafficRuleConfig::RuleId_Name(config_.rule_id()),
                                    frame, reference_line_info);
        }
    }
};

}  // namespace planning
}  // namespace apollo

int main() {
    apollo::planning::TrafficLight a = apollo::planning::TrafficLight();
    apollo::planning::Frame f = apollo::planning::Frame();
    apollo::planning::ReferenceLineInfo r = apollo::planning::ReferenceLineInfo();
    a.ApplyRule(&f, &r);
    return 1;
}