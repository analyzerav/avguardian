#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <vector>

#define CROSSWALK_VO_ID_PREFIX "CROSSWALK"

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
    Vec2d(double x, double y) : x(x), y(y) {}
};
class Polygon2d {
   public:
    Polygon2d ExpandByDistance(double s) { return Polygon2d(); }
    bool IsPointIn(Vec2d v) const { return true; }
};
}  // namespace math
}  // namespace common

namespace perception {
class PerceptionObstacle {
   public:
    enum Type {
        UNKNOWN = 0,
        UNKNOWN_MOVABLE = 1,
        PEDESTRIAN = 2,
        BICYCLE = 3
    };
    Type type() const { return Type(); }
    common::Point3D position() const { return common::Point3D(); }
    common::Point3D velocity() const { return common::Point3D(); }
};
}  // namespace perception

namespace hdmap {
class CrosswalkInfo {
   public:
    std::string id() const { return ""; }
    common::math::Polygon2d polygon() const { return common::math::Polygon2d(); }
};

using CrosswalkInfoConstPtr = std::shared_ptr<const CrosswalkInfo>;
CrosswalkInfoConstPtr GetCrosswalkPtr(std::string id) { return std::shared_ptr<const CrosswalkInfo>(); }

class PathOverlap {
   public:
    double start_s;
    double end_s;
    std::string object_id;
};

class Path {
   public:
    const std::vector<hdmap::PathOverlap> crosswalk_overlaps() const { return std::vector<hdmap::PathOverlap>(); }
};
}  // namespace hdmap

namespace planning {

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
int BuildStopDecision() { return 0; }
}  // namespace util

class Frame {
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
    perception::PerceptionObstacle Perception() const { return perception::PerceptionObstacle(); }
    const SLBoundery PerceptionSLBoundary() const { return SLBoundery(); }
    const STBoundery reference_line_st_boundary() const { return STBoundery(); }
};

class PathDecision {
   public:
    std::vector<Obstacle *> obstacles() { return std::vector<Obstacle *>(); }
};

class ReferenceLine {
   public:
    bool IsOnLane(SLBoundery s) const { return true; }
    bool IsOnRoad(SLBoundery s) const { return true; }
    bool XYToSL(const common::math::Vec2d &v, const common::SLPoint *p) const { return true; }
    hdmap::Path map_path() const { return hdmap::Path(); }
};

class ReferenceLineInfo {
   public:
    PathDecision *path_decision() { return nullptr; }
    SLBoundery AdcSlBoundary() { return SLBoundery(); }
    ReferenceLine reference_line() { return ReferenceLine(); }
};

class Crosswalk_ {
   public:
    double min_pass_s_distance() { return 0; }
    double expand_s_distance() { return 0; }
    double stop_loose_l_distance() { return 0; }
    double stop_strict_l_distance() { return 0; }
    double max_stop_deceleration() { return 0; }
};

class TrafficRuleConfig {
   public:
    Crosswalk_ crosswalk() { return Crosswalk_(); }
};

using apollo::common::Status;
using apollo::common::math::Polygon2d;
using apollo::common::math::Vec2d;
using apollo::hdmap::CrosswalkInfoConstPtr;
using apollo::hdmap::PathOverlap;
using apollo::perception::PerceptionObstacle;
using CrosswalkToStop =
    std::vector<std::pair<const hdmap::PathOverlap *, std::vector<std::string>>>;

class Crosswalk {
   public:
    TrafficRuleConfig config_;
    std::vector<const hdmap::PathOverlap *> crosswalk_overlaps_;

    common::Status ApplyRule(Frame *const frame,
                             ReferenceLineInfo *const reference_line_info) {
        if (!FindCrosswalks(reference_line_info)) {
            return Status::OK();
        }

        MakeDecisions(frame, reference_line_info);
        return Status::OK();
    }

    void MakeDecisions(Frame *const frame,
                       ReferenceLineInfo *const reference_line_info) {
        auto *path_decision = reference_line_info->path_decision();
        double adc_front_edge_s = reference_line_info->AdcSlBoundary().end_s();

        CrosswalkToStop crosswalks_to_stop;

        const auto &reference_line = reference_line_info->reference_line();
        for (auto crosswalk_overlap : crosswalk_overlaps_) {
            auto crosswalk_ptr = hdmap::GetCrosswalkPtr(crosswalk_overlap->object_id);

            // skip crosswalk if master vehicle body already passes the stop line
            if (adc_front_edge_s - crosswalk_overlap->end_s >
                config_.crosswalk().min_pass_s_distance()) {
                continue;
            }

            std::vector<std::string> pedestrians;
            for (const auto *obstacle : path_decision->obstacles()) {
                const double stop_deceleration = util::GetADCStopDeceleration(
                    adc_front_edge_s, crosswalk_overlap->start_s);

                bool stop = CheckStopForObstacle(reference_line_info, crosswalk_ptr,
                                                 *obstacle, stop_deceleration);

                const std::string &obstacle_id = obstacle->Id();
                const PerceptionObstacle &perception_obstacle = obstacle->Perception();
                PerceptionObstacle::Type obstacle_type = perception_obstacle.type();

                if (stop) {
                    pedestrians.push_back(obstacle_id);
                } else {
                }
            }

            if (!pedestrians.empty()) {
                crosswalks_to_stop.emplace_back(crosswalk_overlap, pedestrians);
            }
        }

        double min_s = std::numeric_limits<double>::max();
        hdmap::PathOverlap *firsts_crosswalk_to_stop = nullptr;
        for (auto crosswalk_to_stop : crosswalks_to_stop) {
            // build stop decision
            const auto *crosswalk_overlap = crosswalk_to_stop.first;
            std::string virtual_obstacle_id =
                CROSSWALK_VO_ID_PREFIX + crosswalk_overlap->object_id;
            util::BuildStopDecision();

            if (crosswalk_to_stop.first->start_s < min_s) {
                firsts_crosswalk_to_stop =
                    const_cast<hdmap::PathOverlap *>(crosswalk_to_stop.first);
                min_s = crosswalk_to_stop.first->start_s;
            }
        }
    }

    bool CheckStopForObstacle(
        ReferenceLineInfo *const reference_line_info,
        const CrosswalkInfoConstPtr crosswalk_ptr,
        const Obstacle &obstacle,
        const double stop_deceleration) {
        std::string crosswalk_id = crosswalk_ptr->id();

        const PerceptionObstacle &perception_obstacle = obstacle.Perception();
        const std::string &obstacle_id = obstacle.Id();
        PerceptionObstacle::Type obstacle_type = perception_obstacle.type();
        double adc_end_edge_s = reference_line_info->AdcSlBoundary().start_s();

        // check type
        if (obstacle_type != PerceptionObstacle::PEDESTRIAN &&
            obstacle_type != PerceptionObstacle::BICYCLE &&
            obstacle_type != PerceptionObstacle::UNKNOWN_MOVABLE &&
            obstacle_type != PerceptionObstacle::UNKNOWN) {
            return false;
        }

        // expand crosswalk polygon
        // note: crosswalk expanded area will include sideway area
        Vec2d point(perception_obstacle.position().x(),
                    perception_obstacle.position().y());
        const Polygon2d crosswalk_exp_poly =
            crosswalk_ptr->polygon().ExpandByDistance(
                config_.crosswalk().expand_s_distance());
        bool in_expanded_crosswalk = crosswalk_exp_poly.IsPointIn(point);

        if (!in_expanded_crosswalk) {
            return false;
        }

        const auto &reference_line = reference_line_info->reference_line();

        common::SLPoint obstacle_sl_point;
        reference_line.XYToSL(
            {perception_obstacle.position().x(), perception_obstacle.position().y()},
            &obstacle_sl_point);
        auto &obstacle_sl_boundary = obstacle.PerceptionSLBoundary();
        const double obstacle_l_distance =
            std::min(std::fabs(obstacle_sl_boundary.start_l()),
                     std::fabs(obstacle_sl_boundary.end_l()));

        const bool is_on_lane =
            reference_line.IsOnLane(obstacle.PerceptionSLBoundary());
        const bool is_on_road =
            reference_line.IsOnRoad(obstacle.PerceptionSLBoundary());
        const bool is_path_cross = !obstacle.reference_line_st_boundary().IsEmpty();

        bool stop = false;
        if (obstacle_l_distance >= config_.crosswalk().stop_loose_l_distance()) {
            // (1) when obstacle_l_distance is big enough(>= loose_l_distance),
            //     STOP only if paths crosses
            if (is_path_cross) {
                stop = true;
            }
        } else if (obstacle_l_distance <=
                   config_.crosswalk().stop_strict_l_distance()) {
            if (is_on_road) {
                // (2) when l_distance <= strict_l_distance + on_road
                //     always STOP
                if (obstacle_sl_point.s() > adc_end_edge_s) {
                    stop = true;
                }
            } else {
                // (3) when l_distance <= strict_l_distance
                //     + NOT on_road(i.e. on crosswalk/median etc)
                //     STOP if paths cross
                if (is_path_cross) {
                    stop = true;
                }
            }
        } else {
            // (4) when l_distance is between loose_l and strict_l
            //     use history decision of this crosswalk to smooth unsteadiness

            // TODO(all): replace this temp implementation
            if (is_path_cross) {
                stop = true;
            }
        }

        // check stop_deceleration
        if (stop) {
            if (stop_deceleration >= config_.crosswalk().max_stop_deceleration()) {
                if (obstacle_l_distance > config_.crosswalk().stop_strict_l_distance()) {
                    // SKIP when stop_deceleration is too big but safe to ignore
                    stop = false;
                }
            }
        }

        return stop;
    }

    bool FindCrosswalks(ReferenceLineInfo *const reference_line_info) {
        crosswalk_overlaps_.clear();
        const std::vector<hdmap::PathOverlap> &crosswalk_overlaps =
            reference_line_info->reference_line().map_path().crosswalk_overlaps();
        for (const hdmap::PathOverlap &crosswalk_overlap : crosswalk_overlaps) {
            crosswalk_overlaps_.push_back(&crosswalk_overlap);
        }
        return crosswalk_overlaps_.size() > 0;
    }
};

}  // namespace planning
}  // namespace apollo

int main() {
    apollo::planning::Crosswalk crosswalk = apollo::planning::Crosswalk();
    apollo::planning::Frame f = apollo::planning::Frame();
    apollo::planning::ReferenceLineInfo r = apollo::planning::ReferenceLineInfo();
    crosswalk.ApplyRule(&f, &r);
    return 1;
}