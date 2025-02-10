
class ClassToInitialize {
    static {
        System.out.println("Initializing...");
        try {
            Thread.sleep(1000);
        } catch (InterruptedException e) {
            e.printStackTrace();
        }
        System.out.println("Finished initializing!");
    }
}

public class ConcurrentInitialization extends Thread {
    public static void main(String[] args) {
        Thread t1 = new ConcurrentInitialization();
        Thread t2 = new ConcurrentInitialization();
        Thread t3 = new ConcurrentInitialization();
        t1.start();
        t2.start();
        t3.start();
    }

    public void run() {
        ClassToInitialize c = new ClassToInitialize();
    }
}