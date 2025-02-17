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
                }
            });
            threads.add(t);
            t.start();
        }

        for (int i = 0; i < 10; i++) {
            Thread t = new Thread(() -> {
                for (int j = 0; j < 500; j++) {
                    counter.decrement();
                }
            });
            threads.add(t);
            t.start();
        }

        for (Thread t : threads) {
            try {
                t.join();
            } catch (InterruptedException e) {
                e.printStackTrace();
            }
        }

        System.out.println("Final count: " + counter.getCount()); // should be 5000
    }
}