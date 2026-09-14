module {
  func.func private @opaque(i32) -> i32

  func.func @main(%x: i32) -> i32 {
    %result = func.call @opaque(%x) : (i32) -> i32
    return %result : i32
  }
}
