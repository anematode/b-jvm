
public class SynchronizedCountdownLatch {
    private int count;

    public synchronized void reset(int count) {
        this.count = count;
    }

    public synchronized void awaitCountdown() throws InterruptedException {
        while (count > 0) {
            wait();
        }
    }

    public synchronized void decrement() {
        count--;
        if (count == 0) {
            notifyAll();
        }
    }

    public synchronized void decrementAndAwaitCountdown() throws InterruptedException {
        decrement();
        awaitCountdown();
    }

    public synchronized int getCount() {
        return count;
    }
}