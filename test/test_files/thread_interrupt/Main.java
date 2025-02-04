
public class Main {
    public static void main(String[] args) throws InterruptedException {
        Thread currentThread = Thread.currentThread();
        System.out.println("initially interrupted: " + currentThread.isInterrupted());
        currentThread.interrupt();
        System.out.println("interrupted: " + currentThread.isInterrupted());
        System.out.println("interrupted: " + Thread.interrupted()); // clears the interrupted status
        System.out.println("interrupted: " + currentThread.isInterrupted());

        long start = System.currentTimeMillis();
        Thread.sleep(1000);
        long end = System.currentTimeMillis();
        System.out.println("slept for at least 1000 ms? " + ((end - start) >= 1000));

        System.out.println("interrupting");
        currentThread.interrupt();

        start = System.currentTimeMillis();
        try {
            Thread.sleep(1000);
        } catch (InterruptedException e) {
            System.out.println("interrupted while sleeping");
        }
        end = System.currentTimeMillis();

        System.out.println("interrupted: " + currentThread.isInterrupted());
        System.out.println("slept for at least 1000 ms? " + ((end - start) >= 1000));
    }
}