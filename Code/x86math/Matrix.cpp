/* ---------- headers */

#include "Matrix.h"
#include "3dMath.h"

/* ---------- constants */

// some math functions broke with the compiler
#define MATH_BROKEN NOT_IMPLEMENTED

/* ---------- definitions */

/* ---------- prototypes */

/* ---------- globals */

/* ---------- inline code */

Matrix::Matrix(void) :
	d()
{
}

Matrix::Matrix(const Vector& i, const Vector& j, const Vector& k)
{
	d[0][0] = i.x;
	d[1][0] = i.y;
	d[2][0] = i.z;

	d[0][1] = j.x;
	d[1][1] = j.y;
	d[2][1] = j.z;

	d[0][2] = k.x;
	d[1][2] = k.y;
	d[2][2] = k.z;
}

Matrix::Matrix(const Quaternion& q)
{
	MATH_ENGINE.quaternion_to_matrix(*this, q);
}

Matrix::Matrix(const SINGLE e00, const SINGLE e01, const SINGLE e02,
	const SINGLE e10, const SINGLE e11, const SINGLE e12,
	const SINGLE e20, const SINGLE e21, const SINGLE e22)
{
	d[0][0] = e00;
	d[0][1] = e01;
	d[0][2] = e02;

	d[1][0] = e10;
	d[1][1] = e11;
	d[1][2] = e12;

	d[2][0] = e20;
	d[2][1] = e21;
	d[2][2] = e22;
}

Matrix::Matrix(const PersistMatrix& src)
{
	d[0][0] = src.e00;
	d[0][1] = src.e01;
	d[0][2] = src.e02;
	d[1][0] = src.e10;
	d[1][1] = src.e11;
	d[1][2] = src.e12;
	d[2][0] = src.e20;
	d[2][1] = src.e21;
	d[2][2] = src.e22;
}

SINGLE Matrix::det(void) const
{
	return MATH_ENGINE.det(*this);
}

const Matrix& Matrix::set_identity(void)
{
	d[0][0] = d[1][1] = d[2][2] = 1.0f;

	d[0][1] = d[0][2] =
		d[1][0] = d[1][2] =
		d[2][0] = d[2][1] = 0.0f;

	return *this;
}

void Matrix::make_orthogonal()
{
	Vector i = get_i();
	Vector j = get_j();
	i.fast_normalize();
	j.fast_normalize();
	Vector k = cross_product(i, j);
	k.fast_normalize();
	j = cross_product(k, i);
	j.fast_normalize();
	set_i(i);
	set_j(j);
	set_k(k);
}

void Matrix::rotate_around_i(SINGLE angle)
{
	SINGLE temp[3][3];
	SINGLE cosine, sine;

	angle = -angle;
	cosine = (SINGLE)(cos(angle));
	sine = (SINGLE)(sin(angle));

	memcpy(temp, d, sizeof(d));
	d[0][1] = (cosine * temp[0][1]) - (sine * temp[0][2]);
	d[1][1] = (cosine * temp[1][1]) - (sine * temp[1][2]);
	d[2][1] = (cosine * temp[2][1]) - (sine * temp[2][2]);

	d[0][2] = (cosine * temp[0][2]) + (sine * temp[0][1]);
	d[1][2] = (cosine * temp[1][2]) + (sine * temp[1][1]);
	d[2][2] = (cosine * temp[2][2]) + (sine * temp[2][1]);
}

void Matrix::rotate_around_j(SINGLE angle)
{
	SINGLE temp[3][3];
	SINGLE cosine, sine;

	// angle *= PI_RADIANS; // convert to radians
	cosine = (SINGLE)(cos(angle));
	sine = (SINGLE)(sin(angle));
	memcpy(temp, d, sizeof(d));

	d[0][0] = (cosine * temp[0][0]) + (sine * temp[0][2]);
	d[1][0] = (cosine * temp[1][0]) + (sine * temp[1][2]);
	d[2][0] = (cosine * temp[2][0]) + (sine * temp[2][2]);

	d[0][2] = (cosine * temp[0][2]) - (sine * temp[0][0]);
	d[1][2] = (cosine * temp[1][2]) - (sine * temp[1][0]);
	d[2][2] = (cosine * temp[2][2]) - (sine * temp[2][0]);
}

void Matrix::rotate_around_k(SINGLE angle)
{
	SINGLE temp[3][3];
	SINGLE cosine, sine;

	// angle *= -PI_RADIANS; // convert to radians
	angle = -angle;
	cosine = (SINGLE)(cos(angle));
	sine = (SINGLE)(sin(angle));

	memcpy(temp, d, sizeof(d));

	d[0][0] = (cosine * temp[0][0]) + (sine * temp[0][1]);
	d[1][0] = (cosine * temp[1][0]) + (sine * temp[1][1]);
	d[2][0] = (cosine * temp[2][0]) + (sine * temp[2][1]);

	d[0][1] = (cosine * temp[0][1]) - (sine * temp[0][0]);
	d[1][1] = (cosine * temp[1][1]) - (sine * temp[1][0]);
	d[2][1] = (cosine * temp[2][1]) - (sine * temp[2][0]);
}

const Matrix& Matrix::zero(void)
{
	memset(d, 0, sizeof(SINGLE) * 9);
	return *this;
}

Matrix Matrix::get_transpose(void) const
{
	return Matrix(
		d[0][0], d[1][0], d[2][0],
		d[0][1], d[1][1], d[2][1],
		d[0][2], d[1][2], d[2][2]);
}

Matrix Matrix::get_inverse(void) const
{
	Matrix result;
	MATH_ENGINE.inverse(result, *this);
	return result;
}

const Matrix& Matrix::scale(const SINGLE s)
{
	MATH_ENGINE.scale(*this, *this, s);
	return *this;
}

const Matrix& Matrix::scale_by_reciprocal(const SINGLE s)
{
	MATH_ENGINE.scale(*this, *this, 1.0 / s);
	return *this;
}

const Matrix& Matrix::mul(const Matrix& m)
{
	Matrix result;
	MATH_ENGINE.mul(result, *this, m);
	*this = result;
	return *this;
}

const Matrix& Matrix::operator += (const Matrix& m)
{
	d[0][0] += m.d[0][0];
	d[0][1] += m.d[0][1];
	d[0][2] += m.d[0][2];

	d[1][0] += m.d[1][0];
	d[1][1] += m.d[1][1];
	d[1][2] += m.d[1][2];

	d[2][0] += m.d[2][0];
	d[2][1] += m.d[2][1];
	d[2][2] += m.d[2][2];

	return *this;
}

const Matrix& Matrix::operator -= (const Matrix& m)
{
	d[0][0] -= m.d[0][0];
	d[0][1] -= m.d[0][1];
	d[0][2] -= m.d[0][2];

	d[1][0] -= m.d[1][0];
	d[1][1] -= m.d[1][1];
	d[1][2] -= m.d[1][2];

	d[2][0] -= m.d[2][0];
	d[2][1] -= m.d[2][1];
	d[2][2] -= m.d[2][2];

	return *this;
}

const Matrix& Matrix::operator *= (const SINGLE s)
{
	return scale(s);
}

const Matrix& Matrix::operator /= (const SINGLE s)
{
	return scale_by_reciprocal(s);
}

const Matrix& Matrix::operator *= (const Matrix& m)
{
	return mul(m);
}

#ifdef _INC_MEMORY 
bool Matrix::operator == (const Matrix& m) const
{
	return (0 == memcmp(this, &m, sizeof(*this)));
}

bool Matrix::operator != (const Matrix& m) const
{
	return !(*this == m);
}
#endif

SINGLE Matrix::pow2(const SINGLE f)
{
	// avoids calling fabs below
	return f * f;
}

bool Matrix::equal(const Matrix& m, const SINGLE tolerance) const
{
	const SINGLE tolerance_sq = tolerance * tolerance;
	return ((pow2(d[0][0] - m.d[0][0]) <= tolerance_sq) &&
		(pow2(d[0][1] - m.d[0][1]) <= tolerance_sq) &&
		(pow2(d[0][2] - m.d[0][2]) <= tolerance_sq) &&
		(pow2(d[1][0] - m.d[1][0]) <= tolerance_sq) &&
		(pow2(d[1][1] - m.d[1][1]) <= tolerance_sq) &&
		(pow2(d[1][2] - m.d[1][2]) <= tolerance_sq) &&
		(pow2(d[2][0] - m.d[2][0]) <= tolerance_sq) &&
		(pow2(d[2][1] - m.d[2][1]) <= tolerance_sq) &&
		(pow2(d[2][2] - m.d[2][2]) <= tolerance_sq));
}

Vector Matrix::get_i(void) const
{
	return Vector(d[0][0], d[1][0], d[2][0]);
}

Vector Matrix::get_j(void) const
{
	return Vector(d[0][1], d[1][1], d[2][1]);
}

Vector Matrix::get_k(void) const
{
	return Vector(d[0][2], d[1][2], d[2][2]);
}

void Matrix::set_i(const Vector& i)
{
	d[0][0] = i.x;
	d[1][0] = i.y;
	d[2][0] = i.z;
}

void Matrix::set_j(const Vector& j)
{
	d[0][1] = j.x;
	d[1][1] = j.y;
	d[2][1] = j.z;
}

void Matrix::set_k(const Vector& k)
{
	d[0][2] = k.x;
	d[1][2] = k.y;
	d[2][2] = k.z;
}

void Matrix::compose_rotation(const U32 axis, const SINGLE angle)
{
	switch (axis)
	{
	case PITCH:
		set_x_rotation(angle * MUL_DEG_TO_RAD);
		break;

	case YAW:
		set_y_rotation(angle * MUL_DEG_TO_RAD);
		break;

	case ROLL:
		set_z_rotation(angle * MUL_DEG_TO_RAD);
		break;
	}
}

void Matrix::set_orientation(const SINGLE pitch_degrees, const SINGLE roll_degrees, const SINGLE yaw_degrees)
{
	// Apply yaw, pitch, roll in order ( angles in degrees )
	set_y_rotation(yaw_degrees * MUL_DEG_TO_RAD);
	x_rotate_left(pitch_degrees * MUL_DEG_TO_RAD);
	z_rotate_left(roll_degrees * MUL_DEG_TO_RAD);
}

void Matrix::set_x_rotation(const SINGLE angle) // pitch
{
	const SINGLE _cos = (SINGLE)cos(angle);
	const SINGLE _sin = (SINGLE)sin(angle);

	d[0][0] = 1.0f; d[0][1] = 0.0f; d[0][2] = 0.0f;
	d[1][0] = 0.0f; d[1][1] = _cos; d[1][2] = -_sin;
	d[2][0] = 0.0f; d[2][1] = _sin; d[2][2] = _cos;
}

void Matrix::set_y_rotation(const SINGLE angle) // yaw
{
	const SINGLE _cos = (SINGLE)cos(angle);
	const SINGLE _sin = (SINGLE)sin(angle);

	d[0][0] = _cos; d[0][1] = 0.0f; d[0][2] = _sin;
	d[1][0] = 0.0f; d[1][1] = 1.0f; d[1][2] = 0.0f;
	d[2][0] = -_sin; d[2][1] = 0.0f; d[2][2] = _cos;
}

void Matrix::set_z_rotation(const SINGLE angle) // roll
{
	const SINGLE _cos = (SINGLE)cos(angle);
	const SINGLE _sin = (SINGLE)sin(angle);

	d[0][0] = _cos; d[0][1] = -_sin; d[0][2] = 0.0f;
	d[1][0] = _sin; d[1][1] = _cos; d[1][2] = 0.0f;
	d[2][0] = 0.0f; d[2][1] = 0.0f; d[2][2] = 1.0f;
}

void Matrix::x_rotate_left(const SINGLE angle) // pitch
{
	const SINGLE _cos = (SINGLE)cos(angle);
	const SINGLE _sin = (SINGLE)sin(angle);

	for (int i = 0; i < 3; i++)
	{
		const SINGLE tmp = d[1][i];
		d[1][i] = tmp * _cos - d[2][i] * _sin;
		d[2][i] = tmp * _sin + d[2][i] * _cos;
	}
}

void Matrix::y_rotate_left(const SINGLE angle) // yaw
{
	const SINGLE _cos = (SINGLE)cos(angle);
	const SINGLE _sin = (SINGLE)sin(angle);

	for (int i = 0; i < 3; i++)
	{
		const SINGLE tmp = d[0][i];
		d[0][i] = tmp * _cos + d[2][i] * _sin;
		d[2][i] = d[2][i] * _cos - tmp * _sin;
	}
}

void Matrix::z_rotate_left(const SINGLE angle) // roll
{
	const SINGLE _cos = (SINGLE)cos(angle);
	const SINGLE _sin = (SINGLE)sin(angle);

	for (int i = 0; i < 3; i++)
	{
		const SINGLE tmp = d[0][i];
		d[0][i] = tmp * _cos - d[1][i] * _sin;
		d[1][i] = tmp * _sin + d[1][i] * _cos;
	}
}

void Matrix::x_rotate_right(const SINGLE angle) // pitch
{
	const SINGLE _cos = (SINGLE)cos(angle);
	const SINGLE _sin = (SINGLE)sin(angle);

	for (int i = 0; i < 3; i++)
	{
		const SINGLE tmp = d[i][1];
		d[i][1] = d[i][2] * _sin + tmp * _cos;
		d[i][2] = d[i][2] * _cos - tmp * _sin;
	}
}

void Matrix::y_rotate_right(const SINGLE angle) // yaw
{
	const SINGLE _cos = (SINGLE)cos(angle);
	const SINGLE _sin = (SINGLE)sin(angle);

	for (int i = 0; i < 3; i++)
	{
		const SINGLE tmp = d[i][0];
		d[i][0] = tmp * _cos - d[i][2] * _sin;
		d[i][2] = tmp * _sin + d[i][2] * _cos;
	}
}

void Matrix::z_rotate_right(const SINGLE angle) // roll
{
	const SINGLE _cos = (SINGLE)cos(angle);
	const SINGLE _sin = (SINGLE)sin(angle);

	for (int i = 0; i < 3; i++)
	{
		const SINGLE tmp = d[i][0];
		d[i][0] = d[i][1] * _sin + tmp * _cos;
		d[i][1] = d[i][1] * _cos - tmp * _sin;
	}
}

Matrix operator + (const Matrix& m1, const Matrix& m2)
{
	return add(m1, m2);
}

Matrix operator - (const Matrix& m1, const Matrix& m2)
{
	return subtract(m1, m2);
}

Matrix operator * (const Matrix& m, const SINGLE s)
{
	MATH_BROKEN;
	// return ::mul(m, s);
}

Matrix operator * (const SINGLE s, const Matrix& m)
{
	MATH_BROKEN;
	// return ::mul(m, s);
}

Matrix operator / (const Matrix& m, const SINGLE s)
{
	MATH_BROKEN;
	// return ::mul(m, 1.0f / s);
}

Vector operator * (const Matrix& m, const Vector& v)
{
	MATH_BROKEN;
	//return ::mul(m, v);
}

Vector operator * (const Vector& v, const Matrix& m)
{
	MATH_BROKEN;
	// return ::transpose_mul(m, v);
}

Matrix operator * (const Matrix& m1, const Matrix& m2)
{
	MATH_BROKEN;
	// return ::mul(m1, m2);
}

Matrix mul(const Matrix& m, const SINGLE s)
{
	Matrix result;
	MATH_ENGINE.scale(result, m, s);
	return result;
}

Vector mul(const Matrix& m, const Vector& v)
{
	Vector result;
	MATH_ENGINE.transform(result, m, v);
	return result;
}

Vector transpose_mul(const Matrix& m, const Vector& v)
{
	Vector result;
	MATH_ENGINE.transpose_transform(result, m, v);
	return result;
}

Matrix mul(const Matrix& m1, const Matrix& m2)
{
	Matrix result;
	MATH_ENGINE.mul(result, m1, m2);
	return result;
}

Matrix add(const Matrix& m1, const Matrix& m2)
{
	return Matrix(
		m1.d[0][0] + m2.d[0][0],
		m1.d[0][1] + m2.d[0][1],
		m1.d[0][2] + m2.d[0][2],
		m1.d[1][0] + m2.d[1][0],
		m1.d[1][1] + m2.d[1][1],
		m1.d[1][2] + m2.d[1][2],
		m1.d[2][0] + m2.d[2][0],
		m1.d[2][1] + m2.d[2][1],
		m1.d[2][2] + m2.d[2][2]);
}

Matrix subtract(const Matrix& m1, const Matrix& m2)
{
	return Matrix(
		m1.d[0][0] - m2.d[0][0],
		m1.d[0][1] - m2.d[0][1],
		m1.d[0][2] - m2.d[0][2],
		m1.d[1][0] - m2.d[1][0],
		m1.d[1][1] - m2.d[1][1],
		m1.d[1][2] - m2.d[1][2],
		m1.d[2][0] - m2.d[2][0],
		m1.d[2][1] - m2.d[2][1],
		m1.d[2][2] - m2.d[2][2]);
}

Matrix dual(const Vector& v)
{
	return Matrix(
		0.0f, -v.z, v.y,
		v.z, 0.0f, -v.x,
		-v.y, v.x, 0.0f);
}

/* ---------- private code */

/* ---------- reverse engineering */
