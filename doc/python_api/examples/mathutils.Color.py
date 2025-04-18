import mathutils

# Color values are represented as RGB values from 0 - 1, this is blue.
col = mathutils.Color((0.0, 0.0, 1.0))

# As well as r/g/b attribute access you can adjust them by h/s/v.
col.s *= 0.5

# You can access its components by attribute or index.
print("Color R:", col.r)
print("Color G:", col[1])
print("Color B:", col[-1])
print("Color HSV: {:.2f}, {:.2f}, {:.2f}".format(*col))


# Components of an existing color can be set.
col[:] = 0.0, 0.5, 1.0

# Components of an existing color can use slice notation to get a tuple.
print("Values: {:f}, {:f}, {:f}".format(*col))

# Colors can be added and subtracted.
col += mathutils.Color((0.25, 0.0, 0.0))

# Color can be multiplied, in this example color is scaled to 0-255
# can printed as integers.
print("Color: {:d}, {:d}, {:d}".format(*(int(c) for c in (col * 255.0))))

# This example prints the color as hexadecimal.
print("Hexadecimal: {:02x}{:02x}{:02x}".format(int(col.r * 255), int(col.g * 255), int(col.b * 255)))
