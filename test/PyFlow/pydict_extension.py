import internal.gc as gc

@dataclass(python=True)
class PyDict:
    def copy(self) -> PyDict:
        print("__copy__")
        return self

    def __getitem__(self, key: str) -> str:
        return "__getitem__"

    def __setitem__(self, key: str, val: str):
        print("__setitem__")

    def __len__(self) -> int:
        print("__len__")
        return 0

    def __repr__(self) -> str:
        tmp_str = "hhhhhh"
        len1 = gc.sizeof(tmp_str)
        return f"PyDict: {len1}"

a = PyDict()
b = a.copy()
print(f"a={a}, b={b}")
