import java.lang.ref.WeakReference;
import java.lang.ref.ReferenceQueue;
import java.util.ArrayList;

class Egg {
    public Egg() {

    }

    public String toString() {
        return "Egg";
    }
}

public class WeakReferences {
    public static void main(String[] args) throws InterruptedException {
        Object obj = new Egg();
        Object obj2 = new Egg();
        ReferenceQueue<Object> queue = new ReferenceQueue<Object>();
        ArrayList<WeakReference<Object>> weakRefs = new ArrayList<>();
        for (int i = 0; i < 100; i++) {
            weakRefs.add(new WeakReference<Object>(i < 50 ? obj : obj2, queue));
        }
        System.out.println(weakRefs.get(0).get());
        System.out.println(weakRefs.get(50).get());
        System.out.println(queue.poll());
        obj = null;
        System.gc();
        System.out.println(weakRefs.get(0).get());
        System.out.println(weakRefs.get(50).get());
        for (int i = 0; i < 50; ++i) {
            Object ref = queue.remove();
            if (!weakRefs.contains(ref)) {
                System.out.println("Error: Reference not found in weakRefs");
            }
        }
        System.out.println(queue.poll());
        obj2 = null;
        System.gc();
        for (int i = 50; i < 100; ++i) {
            Object ref = queue.remove();
            if (!weakRefs.contains(ref)) {
                System.out.println("Error: Reference not found in weakRefs");
            }
        }
        System.out.println(queue.poll());
    }
}