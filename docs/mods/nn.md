# Neural network

`nn@3` is a frontend-independent semantic library. Its members are ordinary
Joggle fns, and the compiler core contains no NN operation names.

`matmul`, `relu`, 2-D NCHW `conv`, 2-D NCHW pooling, and inference-mode
`dropout` currently have source bodies.
MatMul is expressed through tensor construction, indexing, reduction, and
scalar arithmetic. Relu is a rank-polymorphic map. Consequently the tensor
fusion pass can compose `matmul → relu` without a MatMul, Relu, or pair-specific
rule.

Conv is likewise tensor construction plus three reductions and scalar index
arithmetic. Its one body covers explicit padding, `VALID`, `SAME_UPPER`,
`SAME_LOWER`, stride, dilation, grouped channels, and optional bias. Padding is
implemented as a predicated scalar contribution with clamped safe reads. The
same generic fusion and loop passes therefore expose its complete computation;
neither pass recognizes the name `conv`.

MaxPool, AveragePool, and GlobalAveragePool use the same construction and
reduction basis. MaxPool clamps padding reads because duplicated values do not
change a maximum. AveragePool predicates padded contributions and derives the
divisor either from the kernel or from a second valid-point reduction. The
bodies cover explicit and SAME padding, stride, ceil mode, MaxPool dilation,
and AveragePool's `count_include_pad` choice. `storage_order` is semantically
irrelevant while this overload returns values rather than ONNX indices.

Concatenation, reshape, flatten, and Softmax currently have typed signatures
and shape functions but no computational bodies. They are honest opaque leaves:
they can represent an imported model, but cannot yet pass through body-derived
fusion or generic loop expansion. These operators also motivate a
rank-polymorphic coordinate value or linear-access basis; encoding each rank in
C++ would violate the extension model.

Adding executable support means writing a portable body or supplying a target
replacement. It does not mean adding an NN subclass, lowering table, fusion
trait, or C++ name switch.
