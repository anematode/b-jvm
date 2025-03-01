public class CollatzConjecture {
    private final FIFOMutex lock = new FIFOMutex();
    private int n; // guarded by lock

    public CollatzConjecture(int n) {
        lock.lock();
        try {
            this.n = n;
        } finally {
            lock.unlock();
        }
    }

    public int getN() {
        lock.lock();
        try {
            return n;
        } finally {
            lock.unlock();
        }
    }

    public int step() {
        lock.lock();
        try {
            if (n == 1) {
                return 1;
            }

            if (n % 2 == 0) {
                n = n / 2;
            } else {
                n = 3 * n + 1;
            }

            return n;
        } finally {
            lock.unlock();
        }
    }

    public int stepAndSleep(long millis) {
        lock.lock();
        try {
            try {
                if (n == 1) {
                    return 1;
                }

                if (n % 2 == 0) {
                    n = n / 2;
                } else {
                    n = 3 * n + 1;
                }

                return n;
            } finally {
                try {
                    Thread.sleep(millis);
                } catch (InterruptedException e) {
                    throw new RuntimeException(e);
                }
            }
        } finally {
            lock.unlock();
        }
    }
}