import mathutils
import math

# Create a new euler with default axis rotation order.
eul = mathutils.Euler((0.0, math.radians(45.0), 0.0), 'XYZ')

# Rotate the euler.
eul.rotate_axis('Z', math.radians(10.0))

# You can access its components by attribute or index.
print("Euler X", eul.x)
print("Euler Y", eul[1])
print("Euler Z", eul[-1])

# Components of an existing euler can be set.
eul[:] = 1.0, 2.0, 3.0

# Components of an existing euler can use slice notation to get a tuple.
print("Values: {:f}, {:f}, {:f}".format(*eul))

# The order can be set at any time too.
eul.order = 'ZYX'

# Eulers can be used to rotate vectors.
vec = mathutils.Vector((0.0, 0.0, 1.0))
vec.rotate(eul)

# Often its useful to convert the euler into a matrix so it can be used as
# transformations with more flexibility.
mat_rot = eul.to_matrix()
mat_loc = mathutils.Matrix.Translation((2.0, 3.0, 4.0))
mat = mat_loc @ mat_rot.to_4x4()
