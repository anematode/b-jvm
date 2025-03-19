import java.util.ArrayList;
import java.lang.reflect.Method;

public class ExternalClass {
    public ExternalClass() throws Exception {
        ArrayList<Integer> list = new ArrayList<>();
        list.add(1);
        list.forEach(ExternalClass::enjoyment);
        Method method = ExternalClass.class.getDeclaredMethod("enjoyment2", ExternalClass.class);
        method.invoke(null, new Object[]{null});
        method = ExternalClass.class.getDeclaredMethod("enjoyment2", ExternalClass[].class);
        method.invoke(null, new Object[]{ new ExternalClass[0] });
    }

    static void enjoyment(int a) {
        System.out.print("ExternalClass instance ");
    }

    static void enjoyment2(ExternalClass d) {
        System.out.print("created");
    }

    static void enjoyment2(ExternalClass[] d) {
        assert d.length == 0;
        System.out.println("!");
    }

    public void someMethod() {
        System.out.println("someMethod() in ExternalClass was called!");
    }
}