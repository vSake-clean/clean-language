# Clean prelude — automatically prepended to every module (v0.2)

enum Option<T>:
    None
    Some(T)

enum Result<T, E>:
    Ok(T)
    Err(E)

trait Ord:
    fn cmp(self, other: Self) -> i64

trait Display:
    fn fmt(self) -> str

trait Clone:
    fn clone(self) -> Self

trait Drop:
    fn drop(self)
