#include <list>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace apollo {

namespace common {
class TrajectoryPoint {
};
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
};
}  // namespace hdmap

namespace planning {

enum StopReasonCode {
    STOP_REASON_STOP_SIGN = 101,
};

class Task {
};

class TaskConfig {
   public:
    enum TaskType {
    };
};

class ScenarioBareIntersectionUnprotectedConfig {
   public:
    bool enable_explicit_stop() { return true; }
    double stop_distance() { return 0; }
    double approach_speed_limit() { return 0; }
};

class SLBoundery {
   public:
    double start_s() const { return 0; }
    double end_s() const { return 0; }
    double start_l() const { return 0; }
    double end_l() const { return 0; }
};

class STPoint {
   public:
    double s() { return 0; }
    double t() { return 0; }
};

class STBoundery {
   public:
    bool IsEmpty() const { return true; }
    double min_t() const { return 0; }
    double min_s() const { return 0; }
    STPoint bottom_left_point() const { return STPoint(); }
    STPoint bottom_right_point() const { return STPoint(); }
};

class Obstacle {
   public:
    std::string Id() const { return ""; }
    bool IsVirtual() { return false; }
    bool IsStatic() { return false; }
    const STBoundery reference_line_st_boundary() const { return STBoundery(); }
};

class PathDecision {
   public:
    std::vector<Obstacle*> obstacles() { return std::vector<Obstacle*>(); }
};

class ReferenceLine {
   public:
    hdmap::Path map_path() const { return hdmap::Path(); }
    void AddSpeedLimit(double start_s, double end_s, double speed_limit);
};

class ReferenceLineInfo {
   public:
    enum OverlapType {
        PNC_JUNCTION = 4,
    };
    ReferenceLine reference_line() { return ReferenceLine(); }
    PathDecision path_decision() const { return PathDecision(); }
    SLBoundery AdcSlBoundary() const { return SLBoundery(); }
    ReferenceLine* mutable_reference_line() { return new ReferenceLine(); }
    void SetJunctionRightOfWay(const double junction_s, const bool is_protected) const {};
};

class Frame {
   public:
    std::list<ReferenceLineInfo> reference_line_info() { return std::list<ReferenceLineInfo>(); }
    std::list<ReferenceLineInfo>* mutable_reference_line_info() { return new std::list<ReferenceLineInfo>(); }
};

namespace util {
int BuildStopDecision(const std::string& stop_wall_id, const double stop_line_s,
                      const double stop_distance,
                      const StopReasonCode& stop_reason_code,
                      const std::vector<std::string>& wait_for_obstacles,
                      const std::string& decision_tag, Frame* const frame,
                      ReferenceLineInfo* const reference_line_info) { return 0; }
}  // namespace util

namespace scenario {

namespace util {

hdmap::PathOverlap* GetOverlapOnReferenceLine(
    const ReferenceLineInfo& reference_line_info, const std::string& overlap_id,
    const ReferenceLineInfo::OverlapType& overlap_type) {
    return new hdmap::PathOverlap();
}

}  // namespace util

class ScenarioConfig {
   public:
    enum StageType {
        BARE_INTERSECTION_UNPROTECTED_INTERSECTION_CRUISE = 202,
    };
    class StageConfig {
       public:
        StageType stage_type() const { return StageType(); }
    };
};

class Stage {
   public:
    enum StageStatus {
        ERROR = 1,
        READY = 2,
        RUNNING = 3,
        FINISHED = 4,
    };

    Stage(const ScenarioConfig::StageConfig& config);

    virtual ~Stage() = default;

    const ScenarioConfig::StageConfig& config() const { return config_; }

    ScenarioConfig::StageType stage_type() const { return config_.stage_type(); }

    /**
   * @brief Each stage does its business logic inside Process function.
   * If the stage want to transit to a different stage after finish,
   * it should set the type of 'next_stage_'.
   */
    virtual StageStatus Process(
        const common::TrajectoryPoint& planning_init_point, Frame* frame) = 0;

    /**
   * @brief The sequence of tasks inside the stage. These tasks usually will be
   * executed in order.
   */
    const std::vector<Task*>& TaskList() const { return task_list_; }

    const std::string& Name() const { return name_; };

    template <typename T>
    T* GetContextAs() {
        return static_cast<T*>(context_);
    }

    void SetContext(void* context) { context_ = context; }

    Task* FindTask(TaskConfig::TaskType task_type) const { return new Task(); };

    ScenarioConfig::StageType NextStage() const { return next_stage_; }

   protected:
    bool ExecuteTaskOnReferenceLine(
        const common::TrajectoryPoint& planning_start_point, Frame* frame) { return true; };

    bool ExecuteTaskOnOpenSpace(Frame* frame) { return true; };

    virtual Stage::StageStatus FinishScenario();

   protected:
    std::map<TaskConfig::TaskType, Task*> tasks_;
    std::vector<Task*> task_list_;
    ScenarioConfig::StageConfig config_;
    ScenarioConfig::StageType next_stage_;
    void* context_ = nullptr;
    std::string name_;
};

#define DECLARE_STAGE(NAME, CONTEXT)                              \
    class NAME : public Stage {                                   \
       public:                                                    \
        explicit NAME(const ScenarioConfig::StageConfig& config)  \
            : Stage(config) {}                                    \
        Stage::StageStatus Process(                               \
            const common::TrajectoryPoint& planning_init_point,   \
            Frame* frame) override;                               \
        CONTEXT* GetContext() { return GetContextAs<CONTEXT>(); } \
    }

namespace bare_intersection {

struct BareIntersectionUnprotectedContext {
    ScenarioBareIntersectionUnprotectedConfig scenario_config;
    std::string current_pnc_junction_overlap_id;
};

class BareIntersectionUnprotectedStageApproach : public Stage {
   public:
    explicit BareIntersectionUnprotectedStageApproach(
        const ScenarioConfig::StageConfig& config)
        : Stage(config) {}

    Stage::StageStatus Process(const common::TrajectoryPoint& planning_init_point,
                               Frame* frame) override;
    BareIntersectionUnprotectedContext* GetContext() {
        return GetContextAs<BareIntersectionUnprotectedContext>();
    }

   private:
    Stage::StageStatus FinishStage();

   private:
    ScenarioBareIntersectionUnprotectedConfig scenario_config_;
    static uint32_t clear_counter_;
};

using common::TrajectoryPoint;
using hdmap::PathOverlap;

uint32_t BareIntersectionUnprotectedStageApproach::clear_counter_ = 0;

Stage::StageStatus BareIntersectionUnprotectedStageApproach::Process(
    const TrajectoryPoint& planning_init_point, Frame* frame) {
    bool plan_ok = ExecuteTaskOnReferenceLine(planning_init_point, frame);

    const auto& reference_line_info = frame->reference_line_info().front();

    const std::string pnc_junction_overlap_id =
        GetContext()->current_pnc_junction_overlap_id;
    if (pnc_junction_overlap_id.empty()) {
        return FinishScenario();
    }

    // get overlap along reference line
    PathOverlap* current_pnc_junction = scenario::util::GetOverlapOnReferenceLine(
        reference_line_info, pnc_junction_overlap_id,
        ReferenceLineInfo::PNC_JUNCTION);
    if (!current_pnc_junction) {
        return FinishScenario();
    }

    constexpr double kPassStopLineBuffer = 0.3;  // unit: m
    const double adc_front_edge_s = reference_line_info.AdcSlBoundary().end_s();
    const double distance_adc_to_pnc_junction =
        current_pnc_junction->start_s - adc_front_edge_s;
    if (distance_adc_to_pnc_junction > kPassStopLineBuffer) {
        // passed stop line
        return FinishStage();
    }

    // set speed_limit to slow down
    if (frame->mutable_reference_line_info()) {
        auto* reference_line =
            frame->mutable_reference_line_info()->front().mutable_reference_line();
        reference_line->AddSpeedLimit(0.0, current_pnc_junction->start_s,
                                      scenario_config_.approach_speed_limit());
    }

    // set right_of_way_status
    reference_line_info.SetJunctionRightOfWay(current_pnc_junction->start_s,
                                              false);

    plan_ok = ExecuteTaskOnReferenceLine(planning_init_point, frame);

    // TODO(all): move to conf
    constexpr double kConf_min_boundary_t = 6.0;        // second
    constexpr double kConf_ignore_max_st_min_t = 0.1;   // second
    constexpr double kConf_ignore_min_st_min_s = 15.0;  // meter

    std::vector<std::string> wait_for_obstacle_ids;
    bool all_far_away = true;
    for (auto* obstacle :
         reference_line_info.path_decision().obstacles()) {
        if (obstacle->IsVirtual() || obstacle->IsStatic()) {
            continue;
        }
        if (obstacle->reference_line_st_boundary().min_t() < kConf_min_boundary_t) {
            const double kepsilon = 1e-6;
            double obstacle_traveled_s =
                obstacle->reference_line_st_boundary().bottom_left_point().s() -
                obstacle->reference_line_st_boundary().bottom_right_point().s();

            // ignore the obstacle which is already on reference line and moving
            // along the direction of ADC
            if (obstacle_traveled_s < kepsilon &&
                obstacle->reference_line_st_boundary().min_t() <
                    kConf_ignore_max_st_min_t &&
                obstacle->reference_line_st_boundary().min_s() >
                    kConf_ignore_min_st_min_s) {
                continue;
            }

            wait_for_obstacle_ids.push_back(obstacle->Id());
            all_far_away = false;
        }
    }

    if (scenario_config_.enable_explicit_stop()) {
        clear_counter_ = all_far_away ? clear_counter_ + 1 : 0;

        bool stop = false;
        constexpr double kCheckClearDistance = 5.0;  // meter
        constexpr double kStartWatchDistance = 2.0;  // meter
        if (distance_adc_to_pnc_junction <= kCheckClearDistance &&
            distance_adc_to_pnc_junction >= kStartWatchDistance && !all_far_away) {
            clear_counter_ = 0;  // reset
            stop = true;
        } else if (distance_adc_to_pnc_junction < kStartWatchDistance) {
            // creeping area
            if (clear_counter_ >= 5) {
                clear_counter_ = 0;  // reset
                return FinishStage();
            } else {
                stop = true;
            }
        }

        if (stop) {
            const std::string virtual_obstacle_id =
                "PNC_JUNCTION_" + current_pnc_junction->object_id;
            planning::util::BuildStopDecision(
                virtual_obstacle_id, current_pnc_junction->start_s,
                scenario_config_.stop_distance(),
                StopReasonCode::STOP_REASON_STOP_SIGN, wait_for_obstacle_ids,
                "bare intersection", frame,
                &(frame->mutable_reference_line_info()->front()));
        }
    } else if (distance_adc_to_pnc_junction <= 0) {
        // rely on st-graph
        return FinishStage();
    }

    return Stage::RUNNING;
}

Stage::StageStatus BareIntersectionUnprotectedStageApproach::FinishStage() {
    next_stage_ =
        ScenarioConfig::BARE_INTERSECTION_UNPROTECTED_INTERSECTION_CRUISE;
    return Stage::FINISHED;
}

}  // namespace bare_intersection
}  // namespace scenario
}  // namespace planning
}  // namespace apollo

int main() {
    const apollo::planning::scenario::ScenarioConfig::StageConfig config = apollo::planning::scenario::ScenarioConfig::StageConfig();
    apollo::planning::scenario::bare_intersection::BareIntersectionUnprotectedStageApproach s = apollo::planning::scenario::bare_intersection::BareIntersectionUnprotectedStageApproach(config);
    apollo::common::TrajectoryPoint p = apollo::common::TrajectoryPoint();
    apollo::planning::Frame f = apollo::planning::Frame();
    s.Process(p, &f);
}