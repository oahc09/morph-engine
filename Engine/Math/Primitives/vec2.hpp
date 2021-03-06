#pragma once
#include <string>

class ivec2;
class vec3;
class uvec2;
namespace std {
  template< typename T >
  class initializer_list;
}

//-----------------------------------------------------------------------------------------------
class vec2 {
public:
  // Construction/Destruction
  vec2() = default; // default constructor: do nothing (for speed)
  ~vec2() = default; // destructor: do nothing (for speed)
  explicit vec2(float v);
  vec2(const vec2& copyFrom); // copy constructor (from another vec2)
  explicit vec2(const ivec2& copyFrom); // convert from ivec2)
  explicit vec2(const uvec2& copyFrom); // convert from ivec2)
  vec2(float initialX, float initialY);
  vec2(const char* str);
  void fromString(const char* data);
  std::string toString() const;

  // Operators
  vec2 operator+(const vec2& vecToAdd) const; // vec2 + vec2
  vec2 operator-(const vec2& vecToSubtract) const; // vec2 - vec2
  vec2 operator-() const;
  vec2 operator*(float uniformScale) const; // vec2 * float
  vec2 operator/(float inverseScale) const; // vec2 / float
  vec2 operator*(const vec2& rhs) const; // component wise multiply
  vec2 operator/(const vec2& rhs) const; // component wise divide
  void operator=(const vec3& copyFrom); // vec2 = vec3
  void operator+=(const vec2& vecToAdd); // vec2 += vec2
  void operator-=(const vec2& vecToSubtract); // vec2 -= vec2
  void operator*=(const float uniformScale); // vec2 *= float
  void operator/=(const float uniformDivisor); // vec2 /= float
  void operator=(const vec2& copyFrom); // vec2 = vec2
  bool operator==(const vec2& compare) const; // vec2 == vec2
  bool operator!=(const vec2& compare) const; // vec2 != vec2

  float magnitude() const;
  float getLength() const;
  float getLengthSquared() const; // faster than GetLength() since it skips the sqrtf()
  float normalizeAndGetLength(); // set my new length to 1.0f; keep my direction
  vec2 normalized() const; // return a new vector, which is a normalized copy of me
  float getOrientationDegrees() const; // return 0 for east (5,0), 90 for north (0,8), etc.
 
  float manhattan() const;
  float manhattan(const vec2& rhs) const;
  float distance(const vec2& another) const;
  float dot(const vec2& another) const;
  void setAngle(float degree);
  static vec2 makeDirectionAtDegrees(float degrees); // create vector at angle
  static float dotProduct(const vec2& a, const vec2& b);
  static float dot(const vec2& a, const vec2& b);
  static float angle(const vec2& a, const vec2& b);
  friend const vec2 operator*(float uniformScale, const vec2& vecToScale); // float * vec2

public:
  float x = 0;
  float y = 0;
  static const vec2 zero;
  static const vec2 one;
  static const vec2 top;
  static const vec2 down;
  static const vec2 left;
  static const vec2 right;
};


// Gets the projected vector in the "projectOnto" direction, whose magnitude is the projected length of "vectorToProject" in that direction.
const vec2 projectTo(const vec2& vectorToProject, const vec2& projectOnto);
const vec2 transform(const vec2& originalVector, 
                        const vec2& fromX, const vec2& fromY,
                        const vec2& toI, const vec2& toJ);

// Returns the vector"s representation/coordinates in (i,j) space (instead of its original x,y space)
const vec2 transToBasis(const vec2& originalVector,
                           const vec2& toBasisI, const vec2& toBasisJ);

// Takes "vectorInBasis" in (i,j) space and returns the equivalent vector in [axis-aligned] (x,y) Cartesian space
const vec2 transFromBasis(const vec2& originalVector,
                        const vec2& fromBasisI, const vec2& fromBasisJ);

// Decomposes "originalVector" into two component vectors, which add up to the original:

//   "vectorAlongI" is the vector portion in the "newBasisI" direction, and
//   "vectorAlongJ" is the vector portion in the "newBasisJ" direction.

void decompose(const vec2& originalVector,
                              const vec2& newBasisI, const vec2& newBasisJ,
                              vec2& out_vectorAlongI, vec2& out_vectorAlongJ);


using Point2 = vec2;