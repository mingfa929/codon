# @test
def test_roundtrip(x: T, T: type):
    assert T.__from_py__(x.__to_py__()) == x

test_roundtrip(42)
# test_roundtrip(3.14)
# test_roundtrip(True)
# test_roundtrip(False)
# test_roundtrip(byte(99))
# test_roundtrip('hello world')
# test_roundtrip('')
# test_roundtrip(List[int]())
test_roundtrip([11, 22, 33])