import java.util.ArrayList;

public class Main {
    public static void main(String[] args) {
        SynchronizedCounter counter = new SynchronizedCounter();
        counter.reset();

        ArrayList<Thread> threads = new ArrayList<>();
        for (int i = 0; i < 10; i++) {
            Thread t = new Thread(() -> {
                for (int j = 0; j < 1000; j++) {
                    counter.increment();
                    Thread.yield(); // just to make things more wild
                }
            });
            threads.add(t);
        }

        for (int i = 0; i < 10; i++) {
            Thread t = new Thread(() -> {
                for (int j = 0; j < 500; j++) {
                    counter.decrement();
                    Thread.yield(); // just to make things more wild
                }
            });
            threads.add(t);
        }

        for (Thread t : threads) {
            t.start();
            // System.out.println("Thread started: " + t.getName());
        }

        for (Thread t : threads) {
            try {
                t.join();
                // System.out.println("Thread joined: " + t.getName());
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }

        System.out.println("Final count: " + counter.getCount()); // should be 5000
    }
}