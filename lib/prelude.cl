# Clean prelude — automatically included in every module (v0.1)
# Note: Example enum definitions. Type params <T, E> parsed but
# monomorphization not implemented — payloads use fixed-size allocation.

enum Option<T>:
    None
    Some(T)

enum Result<T, E>:
    Ok(T)
    Err(E)

trait Ord:
    fn cmp(self, other: Self) -> i32

trait Display:
    fn fmt(self) -> str

trait Clone:
    fn clone(self) -> Self

trait Drop:
    fn drop(self)
