from z3 import *

counter = 0


def get_nounce():
    global counter
    counter += 1
    return str(counter)


class BaseModel:
    def __init__(self, name):
        self.name = name

    def __repr__(self):
        return self.name

    def __str__(self):
        return self.name


class Point(BaseModel):
    def __init__(self, l, s, name=""):
        super().__init__(name)
        self.l = l
        self.s = s


class Line(BaseModel):
    def __init__(self, func, name=""):
        super().__init__(name)
        self.func = func

    def get(self, l, s):
        return self.func(l, s)


class Area(BaseModel):
    def __init__(self, func, name=""):
        super().__init__(name)
        self.func = func

    def get(self, l, s):
        return self.func(l, s)


class CenterArea(Area):
    def __init__(self, center: Point, boundary: Point, name=""):
        super().__init__(name)

        def func(l, s):
            return And(l - center.l <= boundary.l,
                       l - center.l >= -boundary.l,
                       s - center.s <= boundary.s,
                       s - center.s >= -boundary.s)
        self.func = func


class BoundaryArea(Area):
    def __init__(self, start: Point, end: Point, name=""):
        super().__init__(name)

        def func(l, s):
            return And(l >= start.l,
                       l <= end.l,
                       s >= start.s,
                       s <= end.s)
        self.func = func


def get_line_segment(p1, p2, name=""):
    def f(l, s):
        return And((p2.s - p1.s) * (p2.l - l) == (p2.s - s) * (p2.l - p1.l), Or(And(l <= p2.l, l >= p1.l), And(l >= p2.l, l <= p1.l)))
    return Line(f, name)


def get_square_distance(p1, p2):
    return (p2.l - p1.l) ** 2 + (p2.s - p1.s) ** 2


def get_ray(p, v, name=""):
    def f(l, s):
        return And(v.l * (s - p.s) == v.s * (l - p.l), v.s * (s - p.s) >= 0, v.l * (l - p.l) >= 0)
    return Line(f, name)


def get_limited_ray(p, v, length, name=""):
    def f(l, s):
        p2 = Point(l, s)
        return And(get_square_distance(p, p2) <= length ** 2, v.l * (s - p.s) == v.s * (l - p.l), v.s * (s - p.s) >= 0, v.l * (l - p.l) >= 0)
    return Line(f, name)


def get_trajectory(p, v, t, name=""):
    def f(l, s):
        p2 = Point(l, s)
        return And(get_square_distance(p, p2) <= (v.l * t) ** 2 + (v.s * t) ** 2, v.l * (s - p.s) == v.s * (l - p.l), v.s * (s - p.s) >= 0, v.l * (l - p.l) >= 0)
    return Line(f, name)


def z3_abs(v):
    return If(v >= 0, v, -v)


def z3_min(a, b):
    return If(a < b, a, b)


def z3_max(a, b):
    return If(a > b, a, b)


def is_point_on_line(p, f):
    name = str(p) + "_" + str(f) + get_nounce()
    l, s = Reals(name + "_l" + " " + name + "_s")
    return Exists([l, s], And(l == p.l, s == p.s, f.get(l, s)))


def is_point_in_area(p, f):
    name = str(p) + "_" + str(f) + get_nounce()
    l, s = Reals(name + "_l" + " " + name + "_s")
    return Exists([l, s], And(l == p.l, s == p.s, f.get(l, s)))


def is_two_lines_cross(f1, f2):
    name = str(f1) + "_" + str(f2) + get_nounce()
    l, s = Reals(name + "_l" + " " + name + "_s")
    return Exists([l, s], And(f1.get(l, s), f2.get(l, s)))


def is_two_areas_cross(f1, f2):
    name = str(f1) + "_" + str(f2) + get_nounce()
    l, s = Reals(name + "_l" + " " + name + "_s")
    return Exists([l, s], And(f1.get(l, s), f2.get(l, s)))


def is_line_cross_area(f1, f2):
    name = str(f1) + "_" + str(f2) + get_nounce()
    l, s = Reals(name + "_l" + " " + name + "_s")
    return Exists([l, s], And(f1.get(l, s), f2.get(l, s)))


def is_line_in_area(l, a):
    name = str(l) + "_" + str(a) + get_nounce()
    l, s = Reals(name + "_l" + " " + name + "_s")
    return ForAll([l, s], Implies(l.get(l, s), a.get(l, s)))


def is_area_in_area(a1, a2):
    name = str(a1) + "_" + str(a2) + get_nounce()
    l, s = Reals(name + "_l" + " " + name + "_s")
    return ForAll([l, s], Implies(a1.get(l, s), a2.get(l, s)))
