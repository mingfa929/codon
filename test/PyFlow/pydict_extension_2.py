# import internal.python
# internal.python.setup_python(False)

L1 = [1, 2, 3]
ptr = L1.__to_py__()

L2 = List[int].__from_py__(ptr)
print(L2)
