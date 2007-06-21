class ProgressHook(object):
    def __init__(self, hook, range = (0.0, 1.0)):
        self._start = range[0]
        self._end = range[1]
        self._hook = hook

    def start(self):
        return self._start

    def end(self):
        return self._end

    def __call__(self, progress):
        self._hook(self._start + progress * self._length())

    def _length(self):
        return self._end - self._start

    def rangeTicker(self, count):
        return RangeTicker(self, count)

    def subProgress(self, startPercent, endPercent):
        return ProgressHook(
            self,
            (self.start + startPercent * self._length(),
             self.start + endPercent * self._length()))

class RangeTicker(object):
    def __init__(self, progressHook, count):
        self._progressHook = progressHook
        self._count = float(count)
        self._index = 0

    def __call__(self):
        self._index += 1
        self._progressHook(self._index / self._count)

# --------------------------------------------------------------------

import sys, time

class StatusMessage(object):
    def __init__(self, message):
        self._message = message
        self._promille = None
        self._startClock = time.clock()
        self(0.0)

    def __del__(self):
        self.finish()

    def __call__(self, percent):
        promille = int(percent * 1000)
        if promille != self._promille:
            sys.stdout.write("\r%s... (%.1f%%)" % (
                self._message, percent * 100))
            sys.stdout.flush()
            self._promille = promille

    def totalTime(self):
        return self._totalTime

    def finish(self):
        if self._promille:
            self._totalTime = time.clock() - self._startClock
            sys.stdout.write("\r%s... done. (%ss.)\n" % (
                self._message, self._totalTime))
            self._promille = None
