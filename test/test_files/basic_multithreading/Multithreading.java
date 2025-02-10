import java.util.ArrayList;

class MultithreadingDemo implements Runnable {
    volatile static ArrayList<Integer> list = new ArrayList<>();

    public void run() {
        int id = (int)Thread.currentThread().getId();
        System.out.println("Thread " + id + " is now alive!");

        long sleepFor = (int)(Math.random() * 10) * 20;
        System.out.println("Thread " + (int)Thread.currentThread().getId() + " sleeps for " + sleepFor + "ms");
        try {
            Thread.sleep(sleepFor);
        } catch (InterruptedException e) {
            throw new RuntimeException(e);
        }
        System.out.println("Thread " + (int)Thread.currentThread().getId() + " wakes up after " + sleepFor + "ms");
        System.out.println("List is null: " + (list == null));
        list.add((int)sleepFor);

        System.out.println("Thread " + id + " is exiting!");
        for (int i = 0; i < list.size(); i++) {
            System.out.print(list.get(i) + " ");
        }
        System.out.println();
    }
}

public class Multithreading {
    public static void main(String[] args) {
        int n = 8; // Number of threads
        for (int i = 0; i < n; i++) {
            Thread object = new Thread(new MultithreadingDemo());
            object.start();
            System.out.println("Done!\n");
        }
    }
}