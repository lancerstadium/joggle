# Neural network

`nn@2` is a frontend-independent semantic library. Its members are ordinary
Joggle fns, and the compiler core contains no NN operation names.

`matmul`, `relu`, and inference-mode `dropout` currently have source bodies.
MatMul is expressed through tensor construction, indexing, reduction, and
scalar arithmetic. Relu is a rank-polymorphic map. Consequently the tensor
fusion pass can compose `matmul → relu` without a MatMul, Relu, or pair-specific
rule.

Conv, pooling, concatenation, reshape, flatten, and Softmax currently have
typed signatures and shape functions but no computational bodies. They are
honest opaque leaves: they can represent an imported model, but cannot yet pass
through body-derived fusion or generic loop expansion.

Adding executable support means writing a portable body or supplying a target
replacement. It does not mean adding an NN subclass, lowering table, fusion
trait, or C++ name switch.
