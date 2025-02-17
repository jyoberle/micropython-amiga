# test importing of invalid .mpy files

try:
    import sys, io, os

    io.IOBase
    os.mount
except (ImportError, AttributeError):
    print("SKIP")
    raise SystemExit


class UserFile(io.IOBase):
    def __init__(self, data):
        self.data = memoryview(data)
        self.pos = 0

    def readinto(self, buf):
        n = min(len(buf), len(self.data) - self.pos)
        buf[:n] = self.data[self.pos : self.pos + n]
        self.pos += n
        return n

    def ioctl(self, req, arg):
        return 0


class UserFS:
    def __init__(self, files):
        self.files = files

    def mount(self, readonly, mksfs):
        pass

    def umount(self):
        pass

    def stat(self, path):
        if path in self.files:
            return (32768, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        raise OSError

    def open(self, path, mode):
        return UserFile(self.files[path])


# these are the test .mpy files
user_files = {
    "/mod0.mpy": b"",  # empty file
    "/mod1.mpy": b"M",  # too short header
    "/mod2.mpy": b"M\x00\x00\x00",  # bad version
}

# create and mount a user filesystem
os.mount(UserFS(user_files), "/Ram Disk")
sys.path.append("/Ram Disk")

# import .mpy files from the user filesystem
for i in range(len(user_files)):
    mod = "mod%u" % i
    try:
        __import__(mod)
    except ValueError as er:
        print(mod, "ValueError", er)

# unmount and undo path addition
os.umount("/Ram Disk")
sys.path.pop()
