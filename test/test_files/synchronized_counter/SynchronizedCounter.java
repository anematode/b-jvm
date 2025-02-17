
public class SynchronizedCounter {
    private int count;

    public synchronized void reset() {
        count = 0;
    }

    public synchronized void increment() {
        count++;
    }

    public synchronized void decrement() {
        count--;
    }

    public int getCount() {
        return count;
    }
}