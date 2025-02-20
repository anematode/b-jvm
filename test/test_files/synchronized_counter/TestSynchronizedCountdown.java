import java.util.ArrayList;

public class TestSynchronizedCountdown {
    public static void main(String[] args) throws Exception {
        SynchronizedCounter counter = new SynchronizedCounter();
        SynchronizedCountdownLatch latch = new SynchronizedCountdownLatch();
        counter.reset();
        latch.reset(10 * 1000 + 10);

        ArrayList<Thread> threads = new ArrayList<>();
        for (int i = 0; i < 10; i++) {
            Thread t = new Thread(() -> {
                for (int j = 0; j < 1000; j++) {
                    latch.decrement();
                    Thread.yield(); // just to make things more wild
                }
            });
            threads.add(t);
        }

        for (int i = 0; i < 10; i++) {
            Thread t = new Thread(() -> {
                try {
                    latch.decrementAndAwaitCountdown();
                    counter.increment();
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            });
            threads.add(t);
        }

        for (int i = 0; i < 10; i++) {
            Thread t = new Thread(() -> {
                try {
                    latch.awaitCountdown();
                    counter.increment();
                } catch (InterruptedException e) {
                    e.printStackTrace();
                }
            });
            threads.add(t);
        }

        for (Thread t : threads) {
            t.start();
            // System.out.println("Thread started: " + t.getName());
        }

        latch.awaitCountdown();
        counter.increment();

        for (Thread t : threads) {
            try {
                t.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }

        System.out.println("Num arrived: " + counter.getCount()); // should be 21
        System.out.println("Countdown value: " + latch.getCount()); // should be 0
    }
}