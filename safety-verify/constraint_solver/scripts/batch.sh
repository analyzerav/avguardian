DIR=$(dirname $0)
WORKDIR=${DIR}/../

cd $WORKDIR

# python -m av_solver --code data/constraints/stop_crosswalk_constraint.txt --spec data/specs/spec_crosswalk_1.txt --output data/cases/case_crosswalk_1.json --scenario crosswalk --action 0
# python -m av_solver --code data/constraints/stop_destination_constraint.txt --spec data/specs/spec_crosswalk_2.txt --output data/cases/case_crosswalk_2.json --scenario crosswalk --action 1
# python -m av_solver --code data/constraints/stop_crosswalk_constraint.txt --spec data/specs/spec_crosswalk_3.txt --output data/cases/case_crosswalk_3.json --scenario crosswalk --action 0
# python -m av_solver --code data/constraints/stop_crosswalk_constraint.txt --spec data/specs/spec_crosswalk_4.txt --output data/cases/case_crosswalk_4.json --scenario crosswalk --action 0
# python -m av_solver --code data/constraints/stop_destination_constraint.txt --spec data/specs/spec_crosswalk_5.txt --output data/cases/case_crosswalk_5.json --scenario crosswalk --action 1

python -m av_solver --code data/constraints/stop_keep_clear_constraint.txt --spec data/specs/spec_intersection_2.txt --output data/cases/case_intersection_2.json --scenario traffic_light --action 0
python -m av_solver --code data/constraints/stop_traffic_light_constraint.txt --spec data/specs/spec_intersection_3.1.txt --output data/cases/case_intersection_3.1.json --scenario traffic_light --action 0
python -m av_solver --code data/constraints/stop_keep_clear_constraint.txt --spec data/specs/spec_intersection_3.2.txt --output data/cases/case_intersection_3.2.json --scenario traffic_light --action 0
python -m av_solver --code data/constraints/stop_traffic_light_constraint.txt --spec data/specs/spec_intersection_6.1.txt --output data/cases/case_intersection_6.1.json --scenario traffic_light --action 0
python -m av_solver --code data/constraints/stop_keep_clear_constraint.txt --spec data/specs/spec_intersection_6.2.txt --output data/cases/case_intersection_6.2.json --scenario traffic_light --action 0
python -m av_solver --code data/constraints/stop_traffic_light_constraint.txt --spec data/specs/spec_intersection_8.1.txt --output data/cases/case_intersection_8.1.json --scenario traffic_light --action 0
python -m av_solver --code data/constraints/stop_keep_clear_constraint.txt --spec data/specs/spec_intersection_8.2.txt --output data/cases/case_intersection_8.2.json --scenario traffic_light --action 0

python -m av_solver --code data/constraints/stop_traffic_light_constraint.txt --spec data/specs/spec_traffic_light_1.txt --output data/cases/case_traffic_light_1.json --scenario traffic_light --action 0
python -m av_solver --code data/constraints/stop_traffic_light_constraint.txt --spec data/specs/spec_traffic_light_2.txt --output data/cases/case_traffic_light_2.json --scenario traffic_light --action 0

cd -