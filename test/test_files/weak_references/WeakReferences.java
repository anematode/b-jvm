import java.lang.ref.WeakReference;
import java.lang.ref.ReferenceQueue;

public class WeakReferences {
    public static void main(String[] args) throws InterruptedException {
        Object obj = new Object();
        ReferenceQueue<Object> queue = new ReferenceQueue<Object>();
        WeakReference<Object> weakRef = new WeakReference<>(obj, queue);
        obj = null;
        System.out.println(weakRef.get());
        System.out.println(queue.poll());
        System.gc();
        System.out.println(weakRef.get());
        Thread.sleep(1000);
        System.out.println(queue.poll());
    }
}