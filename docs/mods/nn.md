# Neural network

`nn@3` is a frontend-independent semantic library. Its members are ordinary
Joggle fns, and the compiler core contains no NN operation names.

`matmul`, `relu`, 2-D NCHW `conv`, and inference-mode `dropout` currently have
source bodies.
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

Pooling, concatenation, reshape, flatten, and Softmax currently have typed
signatures and shape functions but no computational bodies. They are honest
opaque leaves: they can represent an imported model, but cannot yet pass
through body-derived fusion or generic loop expansion.

Adding executable support means writing a portable body or supplying a target
replacement. It does not mean adding an NN subclass, lowering table, fusion
trait, or C++ name switch.
