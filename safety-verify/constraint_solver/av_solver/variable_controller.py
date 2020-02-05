from z3 import *
from .model import *
from .objects import *
from .scenarios import *
from .api import *
import logging
import re


class VariableController:
    def __init__(self, action_mode=0, scenario_type=""):
        # space -> name -> value
        # space <= [code, model, user]
        # name <= string
        self.var_map = {
            "code": {},
            "model": {},
            "user": {}
        }
        # space -> list of constraints
        # space <= [code, model, user]
        # constraint <= boolean expression
        self.constraints = {
            "code": [],
            "model": [],
            "user": []
        }
        
        self.scenario = None
        self.action_mode = action_mode
        self.scenario_type = scenario_type
        # init
        self.build()

    def get_var(self, space, name):
        if space != None:
            if isinstance(self.var_map, dict) and space in self.var_map and name in self.var_map[space]:
                return self.var_map[space][name]
            else:
                raise Exception("{} not found".format(name))
        else:
            if not isinstance(self.var_map, dict):
                raise Exception("{} not found".format(name))
            for s in ["code", "model", "user"]:
                if s in self.var_map and name in self.var_map[s]:
                    return self.var_map[s][name]
            raise Exception("{} not found".format(name))

    def is_space(self, space):
        return isinstance(self.var_map, dict) and space in self.var_map and isinstance(self.constraints, dict) and space in self.constraints

    def build(self):
        self._build_model_space()
        self._build_code_space()
        self._build_user_space()

    def build_var(self, space, value, name=None):
        if not self.is_space(space):
            raise Exception("Space not found")
        # TODO: check value type here
        if name == None or name == "":
            name = str(value)
        if name in self.var_map[space]:
            logging.warning(
                "Variable {} of space {} already exists. Overwritting.".format(name, space))
        self.var_map[space][name] = value

    def add_constraint(self, space, constraint):
        if not self.is_space(space):
            raise Exception("Space not found")
        self.constraints[space].append(constraint)

    def solve(self):
        # m = True
        # for c in self.constraints["model"]:
        #     print(c)
        #     m = And(m, c)
        #     solve(And(Not(Or(*self.constraints["code"])), Or(*self.constraints["user"]), m))
        s = Solver()

        if self.action_mode == 0:
            s.add(Not(Or(*self.constraints["code"])), And(*self.constraints["model"]), Or(*self.constraints["user"]))
        elif self.action_mode == 1:
            s.add(Or(*self.constraints["code"]), And(*self.constraints["model"]), Or(*self.constraints["user"]))
        else:
            raise Exception("Wrong action mode")
        result = s.check()
        logging.info(result)

        data = {"common": {}}
        labels = ["vehicle", "pedestrian", "crosswalk", "traffic_light", "stop_sign", "yield_sign"]
        for label in labels:
            data[label] = {}
        if result == sat:
            model = s.model()
            logging.info("user policy - code policy > 0, violation found")
            logging.info(model)
            # write to data
            for name in model:
                written = False
                if isinstance(model[name], BoolRef):
                    _value = is_true(model[name])
                elif is_int_value(model[name]):
                    _value = model[name].as_long()
                elif is_rational_value(model[name]):
                    _value = float(model[name].numerator_as_long()) / \
                        float(model[name].denominator_as_long())
                else:
                    continue
                for label in labels:
                    if re.match("^{}_[0-9]+_.*".format(label), str(name)) != None:
                        m = re.match(
                            "^{}_(?P<id>[^_]*)_(?P<key>.*)$".format(label), str(name))
                        _id = m.group('id')
                        _key = m.group('key')
                        if _id not in data[label]:
                            data[label][_id] = {}
                        data[label][_id][_key] = _value
                        written = True
                        break
                if not written:
                    data["common"][str(name)] = _value
        else:
            logging.info("no violation found")
        return data

    def _build_model_space(self):
        # configure this
        if self.scenario_type == "crosswalk":
            self.scenario = CrosswalkScenario()
        elif self.scenario_type == "intersection":
            self.scenario = IntersectionScenario()
        elif self.scenario_type == "traffic_light":
            self.scenario = TrafficLightIntersectionScenario()
        elif self.scenario_type == "stop_sign":
            self.scenario = StopSignIntersectionScenario()
        else:
            self.scenario = FullScenario()
        
        for name, var in self.scenario.get_variables():
            self.build_var("model", var, name)
        for c in self.scenario.get_constraints():
            self.add_constraint("model", c)

    def _build_code_space(self):
        # TODO: provide user interface for the variable mapping

        v = self.scenario.ego_vehicle
        d = self.scenario.get_object(Destination)
        # destination
        self.build_var("code", And(d.pos.s - v.pos.s < 10, d.pos.s - v.pos.s > -10, d.pos.l - v.pos.l < 10, d.pos.l - v.pos.l > 10), "is_near_destination")
        self.build_var("code", False, "has_passed_destination")

        # traffic_rule/crosswalk
        if self.scenario_type == "crosswalk":
            p = self.scenario.get_object(Pedestrian)
            c = self.scenario.get_object(Crosswalk)

            self.build_var("code", v.end_s, "end_s_15")
            self.build_var("code", c.end_s, "end_s_17")
            self.build_var("code", Real("min_pass_s_distance_19"))
            self.add_constraint("model", self.get_var("code", "min_pass_s_distance_19") == 0)
            self.build_var("code", is_two_areas_cross(p.boundary, c.boundary), "IsPointIn_19")
            self.build_var("code", z3_abs(p.pos.l), "min_24")
            self.build_var("code", Real("stop_loose_l_distance_28"))
            self.add_constraint("model", self.get_var("code", "stop_loose_l_distance_28") == 6)
            self.build_var("code", Real("stop_strict_l_distance_32"))
            self.add_constraint("model", self.get_var("code", "stop_strict_l_distance_32") == 4)
            self.build_var("code", Real("stop_strict_l_distance_29"))
            self.add_constraint("model", self.get_var("code", "stop_strict_l_distance_29") == 4)
            # self.build_var("code", is_two_lines_cross(v.trajectory, p.trajectory), "IsEmpty_26")
            self.build_var("code", api_st_cross(v, p), "IsEmpty_26")
            # TODO: decelaration limit is ignored
            self.build_var("code", Real("max_stop_deceleration_31"))
            self.add_constraint("model", self.get_var("code", "max_stop_deceleration_31") == 2)
            self.build_var("code", Real("stop_deceleration.addr_12"))
            self.add_constraint("model", self.get_var("code", "stop_deceleration.addr_12") == 1)
            self.build_var("code", Bool("FindCrosswalks_3"))
            self.add_constraint("model", self.get_var("code", "FindCrosswalks_3") == True)
            self.build_var("code", is_two_areas_cross(p.boundary, self.scenario.road_boundary), "IsOnRoad_25")
            self.build_var("code", p.pos.s, "s_30")
            self.build_var("code", v.start_s, "start_s_14")
            self.build_var("code", Real("type_13"))
            self.add_constraint("model", self.get_var("code", "type_13") == 3)

        # traffic_rule/traffic_light
        elif self.scenario_type == "traffic_light":
            t = self.scenario.objects["traffic_light_self"]

            self.build_var("code", t.start_s, "start_s60_33")
            self.build_var("code", v.end_s, "end_s_19")
            self.build_var("code", t.end_s, "end_s_21")
            self.build_var("code", v.start_s, "start_s_20")
            self.build_var("code", t, "color_35")
            self.build_var("code", self.scenario.max_stop_deceleration, "max_stop_deceleration_38")
            self.build_var("code", v.velocity / 2 / (t.start_s - v.end_s), "GetADCStopDeceleration_37")



    def _build_user_space(self):
        pass
