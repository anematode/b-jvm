// I have manually patched these files so that they are broken.

public class IllegalMonitors {
    public static void main(String[] args) {
        try {
            test1();
        } catch (IllegalMonitorStateException e) {
            System.out.println("Caught exception1");
            System.out.println(e.getMessage());
        }

        try {
            test2();
        } catch (IllegalMonitorStateException e) {
            System.out.println("Caught exception2");
            System.out.println(e.getMessage());
        }

        try {
            new IllegalMonitors().test3();
        } catch (IllegalMonitorStateException e) {
            System.out.println("Caught exception3");
            System.out.println(e.getMessage());
        }

        try {
            test4();
        } catch (IllegalMonitorStateException e) {
            System.out.println("Caught exception4");
            System.out.println(e.getMessage());
        }

        try {
            test5();
        } catch (IllegalMonitorStateException e) {
            System.out.println("Caught exception5");
            System.out.println(e.getMessage());
        }

        try {
            test6();
        } catch (NullPointerException e) {
            System.out.println("Caught exception6");
            System.out.println(e.getMessage());
        }

        try {
            test7();
        } catch (NullPointerException e) {
            System.out.println("Caught exception7");
            System.out.println(e.getMessage());
        }
    }

    public static void test1() {
        Object o = new Object();
        synchronized (o) {  // monitorenter instruction and exception handler monitorexit nop-ed out
            o.hashCode();
        }
    }

    public static void test2() {
        Object o = new Object();
        synchronized (o) {  // first monitorenter instruction and exception handler monitorexit nop-ed out
            o.hashCode();
            synchronized (o) {
                o.hashCode();
            }
        }
    }

    public synchronized void test3() {
        synchronized (this) {  // monitorenter instruction nop-ed out
            hashCode();
        }
    }

    public static synchronized void test4() {
        synchronized (IllegalMonitors.class) {  // monitorenter instruction and athrow nop-ed out
            IllegalMonitors.class.hashCode();
        }
    }

    public static synchronized void test5() {
        synchronized (IllegalMonitors.class) {  // monitorexit instructions nop-ed out
            throw new Error();  // will get replaced with an illegal monitor exception
        }
    }

    public static void test6() {
        Object o = null;
        synchronized (o) {  // should NPE
            o.hashCode();
        }
    }

    public static void test7() {
        Object o = null;
        synchronized (o) {  // monitorenter instruction replaced with monitorexit
            o.hashCode();
        }
    }
}