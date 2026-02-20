#pragma once

struct Rgba8 
{
public:
    unsigned char r = 255;
    unsigned char g = 255;
    unsigned char b = 255;
    unsigned char a = 255;

    Rgba8() = default; 
    explicit Rgba8(unsigned char red, unsigned char green, unsigned char blue, unsigned char alpha = 255);

	static const Rgba8 WHITE;
	static const Rgba8 BLACK;
	static const Rgba8 RED;
	static const Rgba8 GREEN;
	static const Rgba8 BLUE;
	static const Rgba8 YELLOW;
	static const Rgba8 GREY;
	static const Rgba8 DARKGREY;
	static const Rgba8 TEAL;
	static const Rgba8 CYAN;
	static const Rgba8 MAGENTA;
	static const Rgba8 AQUA;
	static const Rgba8 MINTGREEN;
	static const Rgba8 PEACH;
	static const Rgba8 LAVENDER;
	static const Rgba8 MISTBLUE;

    void SetFromText(char const* text);
    void GetAsFloats(float* colorAsFloats) const;

	bool operator==(const Rgba8& other) const
	{
		return (r == other.r) && (g == other.g) && (b == other.b) && (a == other.a);
	}
	bool operator!=(const Rgba8& other) const
	{
		return (r != other.r) | (g != other.g) | (b != other.b) | (a != other.a);
	}
};

Rgba8 InterpolateRgba8(Rgba8 start, Rgba8 end, float fractionOfEnd);

//const Rgba8 WHITE = Rgba8(255, 255, 255, 255);
//const Rgba8 BLACK = Rgba8(0, 0, 0, 255);
//const Rgba8 RED = Rgba8(255, 0, 0, 255);
//const Rgba8 GREEN = Rgba8(0, 255, 0, 255);
//const Rgba8 BLUE = Rgba8(0, 0, 255, 255);
//const Rgba8 YELLOW = Rgba8(255, 255, 0, 255);
//const Rgba8 GREY = Rgba8(120, 120, 120, 255);
//const Rgba8 TEAL = Rgba8(0, 190, 190, 255);
//const Rgba8 MAGENTA = Rgba8(255, 0, 255, 255);
//const Rgba8 AQUA = Rgba8(127, 185, 212, 255);
//const Rgba8 MINTGREEN = Rgba8(152, 255, 152, 255);
//const Rgba8 PEACH = Rgba8(255, 218, 185, 255);
//const Rgba8 LAVENDER = Rgba8(200, 200, 250, 255);
//const Rgba8 MISTBLUE = Rgba8(187, 204, 224, 255);



