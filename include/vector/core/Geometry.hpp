// VECTOR - Viewport Engine for Concurrent Topology & Obstacle-avoiding Routing
// Copyright (C) 2026 VECTOR Contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef VECTOR_CORE_GEOMETRY_HPP
#define VECTOR_CORE_GEOMETRY_HPP

#include <algorithm>
#include <cmath>
#include <concepts>
#include <numbers>
#include <optional>
#include <vector>

namespace vector::core {

enum class BooleanOp {
  Union,
  Intersection,
  Difference,
  Xor,
  Or = Union,
  And = Intersection,
  Subtract = Difference
};

template <typename T>
concept Numeric = std::is_arithmetic_v<T>;

struct Vec2d {
  double x{0.0};
  double y{0.0};

  constexpr Vec2d() noexcept = default;
  constexpr Vec2d(double in_x, double in_y) noexcept : x(in_x), y(in_y) {}

  constexpr bool operator==(const Vec2d& other) const noexcept {
    return x == other.x && y == other.y;
  }

  constexpr bool operator!=(const Vec2d& other) const noexcept {
    return !(*this == other);
  }

  constexpr Vec2d operator+(const Vec2d& other) const noexcept {
    return {x + other.x, y + other.y};
  }

  constexpr Vec2d operator-(const Vec2d& other) const noexcept {
    return {x - other.x, y - other.y};
  }

  constexpr Vec2d operator*(double scalar) const noexcept {
    return {x * scalar, y * scalar};
  }

  constexpr Vec2d operator/(double scalar) const noexcept {
    return {x / scalar, y / scalar};
  }

  constexpr Vec2d operator-() const noexcept {
    return {-x, -y};
  }

  constexpr Vec2d& operator+=(const Vec2d& other) noexcept {
    x += other.x;
    y += other.y;
    return *this;
  }

  constexpr Vec2d& operator-=(const Vec2d& other) noexcept {
    x -= other.x;
    y -= other.y;
    return *this;
  }

  constexpr Vec2d& operator*=(double scalar) noexcept {
    x *= scalar;
    y *= scalar;
    return *this;
  }

  constexpr Vec2d& operator/=(double scalar) noexcept {
    x /= scalar;
    y /= scalar;
    return *this;
  }

  [[nodiscard]] constexpr double dot(const Vec2d& other) const noexcept {
    return x * other.x + y * other.y;
  }

  [[nodiscard]] constexpr double cross(const Vec2d& other) const noexcept {
    return x * other.y - y * other.x;
  }

  [[nodiscard]] constexpr double length_sq() const noexcept {
    return x * x + y * y;
  }

  [[nodiscard]] double length() const noexcept {
    return std::sqrt(length_sq());
  }

  [[nodiscard]] Vec2d normalized() const noexcept {
    const double len = length();
    if (len > 1e-12) {
      return {x / len, y / len};
    }
    return {0.0, 0.0};
  }

  [[nodiscard]] constexpr double distance_sq_to(const Vec2d& other) const noexcept {
    const double dx = x - other.x;
    const double dy = y - other.y;
    return dx * dx + dy * dy;
  }

  [[nodiscard]] double distance_to(const Vec2d& other) const noexcept {
    return std::sqrt(distance_sq_to(other));
  }

  [[nodiscard]] constexpr Vec2d lerp(const Vec2d& target, double t) const noexcept {
    return {x + (target.x - x) * t, y + (target.y - y) * t};
  }
};

constexpr Vec2d operator*(double scalar, const Vec2d& vec) noexcept {
  return vec * scalar;
}

struct Size2d {
  double width{0.0};
  double height{0.0};

  constexpr Size2d() noexcept = default;
  constexpr Size2d(double w, double h) noexcept : width(w), height(h) {}

  [[nodiscard]] constexpr bool is_empty() const noexcept {
    return width <= 0.0 || height <= 0.0;
  }

  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return width > 0.0 && height > 0.0;
  }

  [[nodiscard]] constexpr double area() const noexcept {
    return width * height;
  }

  constexpr bool operator==(const Size2d& other) const noexcept {
    return width == other.width && height == other.height;
  }

  constexpr bool operator!=(const Size2d& other) const noexcept {
    return !(*this == other);
  }
};

struct Margin2d {
  double top{0.0};
  double right{0.0};
  double bottom{0.0};
  double left{0.0};

  constexpr Margin2d() noexcept = default;
  constexpr Margin2d(double all) noexcept : top(all), right(all), bottom(all), left(all) {}
  constexpr Margin2d(double vert, double horiz) noexcept
    : top(vert), right(horiz), bottom(vert), left(horiz) {}
  constexpr Margin2d(double t, double r, double b, double l) noexcept
    : top(t), right(r), bottom(b), left(l) {}
};

struct Rect2d {
  double x{0.0};
  double y{0.0};
  double width{0.0};
  double height{0.0};

  constexpr Rect2d() noexcept = default;
  constexpr Rect2d(double in_x, double in_y, double in_w, double in_h) noexcept
    : x(in_x), y(in_y), width(in_w), height(in_h) {}
  constexpr Rect2d(Vec2d origin, Size2d size) noexcept
    : x(origin.x), y(origin.y), width(size.width), height(size.height) {}

  static constexpr Rect2d from_corners(Vec2d p1, Vec2d p2) noexcept {
    const double min_x = (p1.x < p2.x) ? p1.x : p2.x;
    const double min_y = (p1.y < p2.y) ? p1.y : p2.y;
    const double max_x = (p1.x > p2.x) ? p1.x : p2.x;
    const double max_y = (p1.y > p2.y) ? p1.y : p2.y;
    return {min_x, min_y, max_x - min_x, max_y - min_y};
  }

  [[nodiscard]] constexpr double left() const noexcept {
    return x;
  }
  [[nodiscard]] constexpr double top() const noexcept {
    return y;
  }
  [[nodiscard]] constexpr double right() const noexcept {
    return x + width;
  }
  [[nodiscard]] constexpr double bottom() const noexcept {
    return y + height;
  }

  [[nodiscard]] constexpr Vec2d top_left() const noexcept {
    return {x, y};
  }
  [[nodiscard]] constexpr Vec2d top_right() const noexcept {
    return {x + width, y};
  }
  [[nodiscard]] constexpr Vec2d bottom_left() const noexcept {
    return {x, y + height};
  }
  [[nodiscard]] constexpr Vec2d bottom_right() const noexcept {
    return {x + width, y + height};
  }

  [[nodiscard]] constexpr Vec2d center() const noexcept {
    return {x + width * 0.5, y + height * 0.5};
  }

  [[nodiscard]] constexpr Size2d size() const noexcept {
    return {width, height};
  }

  [[nodiscard]] constexpr bool is_empty() const noexcept {
    return width <= 0.0 || height <= 0.0;
  }

  [[nodiscard]] constexpr bool is_valid() const noexcept {
    return width > 0.0 && height > 0.0;
  }

  [[nodiscard]] constexpr double area() const noexcept {
    return is_empty() ? 0.0 : width * height;
  }

  [[nodiscard]] constexpr bool contains(Vec2d pt) const noexcept {
    return pt.x >= x && pt.x <= (x + width) && pt.y >= y && pt.y <= (y + height);
  }

  [[nodiscard]] constexpr bool intersects(const Rect2d& other) const noexcept {
    return left() <= other.right() && right() >= other.left() && top() <= other.bottom() &&
           bottom() >= other.top();
  }

  [[nodiscard]] constexpr Rect2d united(const Rect2d& other) const noexcept {
    if (is_empty())
      return other;
    if (other.is_empty())
      return *this;

    const double min_x = (left() < other.left()) ? left() : other.left();
    const double min_y = (top() < other.top()) ? top() : other.top();
    const double max_x = (right() > other.right()) ? right() : other.right();
    const double max_y = (bottom() > other.bottom()) ? bottom() : other.bottom();
    return {min_x, min_y, max_x - min_x, max_y - min_y};
  }

  [[nodiscard]] constexpr Rect2d expanded(double margin) const noexcept {
    return {x - margin, y - margin, width + 2.0 * margin, height + 2.0 * margin};
  }

  [[nodiscard]] constexpr Rect2d expanded(const Margin2d& m) const noexcept {
    return {x - m.left, y - m.top, width + m.left + m.right, height + m.top + m.bottom};
  }

  constexpr bool operator==(const Rect2d& other) const noexcept {
    return x == other.x && y == other.y && width == other.width && height == other.height;
  }

  constexpr bool operator!=(const Rect2d& other) const noexcept {
    return !(*this == other);
  }
};

class Transform2d {
public:
  // Matrix entries in row-major order:
  // [ m00  m01  m02 ]
  // [ m10  m11  m12 ]
  // [  0    0    1  ]
  double m00{1.0};
  double m01{0.0};
  double m02{0.0};
  double m10{0.0};
  double m11{1.0};
  double m12{0.0};

  constexpr Transform2d() noexcept = default;
  constexpr Transform2d(double in_00, double in_01, double in_02, double in_10, double in_11,
                        double in_12) noexcept
    : m00(in_00), m01(in_01), m02(in_02), m10(in_10), m11(in_11), m12(in_12) {}

  static constexpr Transform2d identity() noexcept {
    return {};
  }

  static constexpr Transform2d translation(double dx, double dy) noexcept {
    return {1.0, 0.0, dx, 0.0, 1.0, dy};
  }

  static constexpr Transform2d scale(double sx, double sy) noexcept {
    return {sx, 0.0, 0.0, 0.0, sy, 0.0};
  }

  static Transform2d rotation(double radians) noexcept {
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    return {c, -s, 0.0, s, c, 0.0};
  }

  [[nodiscard]] constexpr Vec2d map_point(Vec2d pt) const noexcept {
    return {m00 * pt.x + m01 * pt.y + m02, m10 * pt.x + m11 * pt.y + m12};
  }

  [[nodiscard]] Rect2d map_rect(const Rect2d& rect) const noexcept {
    const Vec2d p1 = map_point(rect.top_left());
    const Vec2d p2 = map_point(rect.top_right());
    const Vec2d p3 = map_point(rect.bottom_left());
    const Vec2d p4 = map_point(rect.bottom_right());

    const double min_x = std::min({p1.x, p2.x, p3.x, p4.x});
    const double max_x = std::max({p1.x, p2.x, p3.x, p4.x});
    const double min_y = std::min({p1.y, p2.y, p3.y, p4.y});
    const double max_y = std::max({p1.y, p2.y, p3.y, p4.y});

    return {min_x, min_y, max_x - min_x, max_y - min_y};
  }

  [[nodiscard]] constexpr Transform2d operator*(const Transform2d& rhs) const noexcept {
    return {m00 * rhs.m00 + m01 * rhs.m10,       m00 * rhs.m01 + m01 * rhs.m11,
            m00 * rhs.m02 + m01 * rhs.m12 + m02,

            m10 * rhs.m00 + m11 * rhs.m10,       m10 * rhs.m01 + m11 * rhs.m11,
            m10 * rhs.m02 + m11 * rhs.m12 + m12};
  }

  [[nodiscard]] std::optional<Transform2d> inverted() const noexcept {
    const double det = m00 * m11 - m01 * m10;
    if (std::abs(det) < 1e-14) {
      return std::nullopt;
    }
    const double inv_det = 1.0 / det;
    return Transform2d{m11 * inv_det,  -m01 * inv_det, (m01 * m12 - m02 * m11) * inv_det,
                       -m10 * inv_det, m00 * inv_det,  (m02 * m10 - m00 * m12) * inv_det};
  }

  constexpr bool operator==(const Transform2d& other) const noexcept {
    return m00 == other.m00 && m01 == other.m01 && m02 == other.m02 && m10 == other.m10 &&
           m11 == other.m11 && m12 == other.m12;
  }
};

class Polygon2d {
public:
  std::vector<Vec2d> vertices;

  Polygon2d() = default;
  explicit Polygon2d(std::vector<Vec2d> in_vertices) : vertices(std::move(in_vertices)) {}

  [[nodiscard]] Rect2d bounding_box() const noexcept {
    if (vertices.empty())
      return {};
    double min_x = vertices[0].x;
    double max_x = vertices[0].x;
    double min_y = vertices[0].y;
    double max_y = vertices[0].y;

    for (size_t i = 1; i < vertices.size(); ++i) {
      if (vertices[i].x < min_x)
        min_x = vertices[i].x;
      if (vertices[i].x > max_x)
        max_x = vertices[i].x;
      if (vertices[i].y < min_y)
        min_y = vertices[i].y;
      if (vertices[i].y > max_y)
        max_y = vertices[i].y;
    }
    return {min_x, min_y, max_x - min_x, max_y - min_y};
  }

  [[nodiscard]] bool contains(Vec2d pt) const noexcept {
    if (vertices.size() < 3)
      return false;
    bool inside = false;
    const size_t count = vertices.size();
    for (size_t i = 0, j = count - 1; i < count; j = i++) {
      const Vec2d& vi = vertices[i];
      const Vec2d& vj = vertices[j];
      if (((vi.y > pt.y) != (vj.y > pt.y)) &&
          (pt.x < (vj.x - vi.x) * (pt.y - vi.y) / (vj.y - vi.y) + vi.x)) {
        inside = !inside;
      }
    }
    return inside;
  }
};

template <typename T>
concept SpatialEntity = requires(T entity) {
  { entity.bounding_box() } -> std::same_as<Rect2d>;
};

}  // namespace vector::core

namespace madi = vector;

#endif  // VECTOR_CORE_GEOMETRY_HPP
