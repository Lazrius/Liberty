#pragma once
#ifndef __TRANSFORM_H__
#define __TRANSFORM_H__

/* ---------- headers */

#include "Matrix.h"

/* ---------- constants */

#ifndef PI // #TODO: Move into global math constants header
#define PI 3.14159265358979323846f
#endif

#ifndef MUL_DEG_TO_RAD // #TODO: Move into global math constants header
#define MUL_DEG_TO_RAD (PI / 180.0f)
#endif

/* ---------- definitions */

// 3x4 Transform class.
class X86MATH_DEC Transform : public Matrix
{
public:
	/* ---------- member variables */

	// 4x4 matrix in [row][col] order -- 4th row is implicit { 0 0 0 1 }
	Vector translation;
	//SINGLE element[3][4];

	/* ---------- constructors & destructors */

	Transform(void);
	Transform(const Vector& i, const Vector& j, const Vector& k);
	Transform(const Transform& transform);
	Transform(const Matrix& matrix);
	Transform(const Matrix& rot, const Vector& trans);
	Transform(const Vector& trans);
	// #TODO Make a global NO_INITIALIZATION enum to ensure this is strict
	explicit Transform(bool); // enable construction of a Transform without initialization
	Transform(const PersistTransform& src);

	/* ---------- accessors */

	Transform get_transpose(void) const;
	Transform get_inverse(void) const;
	Transform get_general_inverse(SINGLE& w) const; // assumes last row of 4x4 is 0 0 0 1
	const Vector& get_position(void) const;
	const Matrix& get_orientation(void) const;

	void set_identity(void);
	void set_position(const SINGLE x, const SINGLE y, const SINGLE z);
	void set_position(const Vector& pos);
	void set_orientation(const SINGLE pitch_degrees, const SINGLE roll_degrees, const SINGLE yaw_degrees); // expects degrees
	void set_orientation(const Matrix& mat);
	void set_orientation(const Quaternion& q);

	/* ---------- member functions */

	void rotate_about_i(SINGLE angle); // pitch
	void rotate_about_j(SINGLE angle); // yaw
	void rotate_about_k(SINGLE angle); // roll

	void move_position(const SINGLE dx, const SINGLE dy, const SINGLE dz);
	void move_position(const Vector& delta);

	Vector rotate(const Vector& v) const;
	Vector rotate_translate(const Vector& v) const;
	Vector inverse_rotate(const Vector& v) const;
	Vector inverse_rotate_translate(const Vector& v) const;
	Transform multiply(const Transform& t) const;
	void compose_rotation(const U32 axis, const SINGLE angle); // expects degrees
	bool equal(const Transform& t, const SINGLE tolerance) const;

	Transform& operator = (const Matrix& matrix);
	const Transform& operator *= (const Transform& t);

#ifdef _INC_MEMORY 
	bool operator == (const Transform& t) const;
	bool operator != (const Transform& t) const;
#endif

	// NOTE: The following two operators are NOT equivalent, as matrix multiplication is not commutative.
	//
	// mat * vec performs pre-multiplication with a column vector, and
	// vec * mat performs post-multiplication with a row vector.
	//
	// mat * vec is equivalent to mul(mat, vec),
	// while vec * mat is equivalent to transpose_mul(mat, vec).
	// These operators are provided for completeness.

	X86MATH_DEC friend Vector operator * (const Transform& t, const Vector& v);
	X86MATH_DEC friend Vector operator * (const Vector& v, const Transform& t);
	X86MATH_DEC friend Transform operator * (const Transform& t1, const Transform& t2);
	X86MATH_DEC friend Transform operator + (const Transform& t1, const Transform& t2);
	X86MATH_DEC friend Transform operator - (const Transform& t1, const Transform& t2);
	X86MATH_DEC friend Transform add(const Transform& m1, const Transform& m2);
	X86MATH_DEC friend Transform subtract(const Transform& m1, const Transform& m2);

};

/* ---------- prototypes */

/* ---------- globals */

X86MATH_DEC extern Vector operator * (const Transform& t, const Vector& v);
X86MATH_DEC extern Vector operator * (const Vector& v, const Transform& t);
X86MATH_DEC extern Transform operator * (const Transform& t1, const Transform& t2);
X86MATH_DEC extern Transform operator + (const Transform& t1, const Transform& t2);
X86MATH_DEC extern Transform operator - (const Transform& t1, const Transform& t2);
X86MATH_DEC extern Transform add(const Transform& m1, const Transform& m2);
X86MATH_DEC extern Transform subtract(const Transform& m1, const Transform& m2);

/* ----------  code */

#endif // __TRANSFORM_H__
