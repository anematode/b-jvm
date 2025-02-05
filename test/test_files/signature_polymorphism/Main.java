import java.lang.invoke.*;
import static java.lang.invoke.MethodHandles.*;
import static java.lang.invoke.MethodType.*;
import java.util.*;

public class Main {
    public static void assertEquals(Object a, Object b) {
        DCHECK(a.equals(b));
    }

    public static void main(String[] args) throws Throwable {
        Object x, y; String s; int i;
        MethodType mt; MethodHandle mh;
        MethodHandles.Lookup lookup = MethodHandles.lookup();
        // mt is (char,char)String
        mt = MethodType.methodType(String.class, char.class, char.class);
        mh = lookup.findVirtual(String.class, "replace", mt);
        s = (String) mh.invokeExact("daddy",'d','n');
        // invokeExact(Ljava/lang/String;CC)Ljava/lang/String;
        System.out.println(s);
        assertEquals(s, "nanny");
        // weakly typed invocation (using MHs.invoke)
        s = (String) mh.invokeWithArguments("sappy", 'p', 'v');
        System.out.println(s);
        assertEquals(s, "savvy");
        // mt is (Object[])List
        mt = MethodType.methodType(java.util.List.class, Object[].class);
        mh = lookup.findStatic(java.util.Arrays.class, "asList", mt);
        DCHECK(mh.isVarargsCollector());
        x = mh.invoke("one", "two");
        // invoke(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/Object;
        assertEquals(x, java.util.Arrays.asList("one","two"));
        // mt is (Object,Object,Object)Object
        mt = MethodType.genericMethodType(3);
        mh = mh.asType(mt);
        x = mh.invokeExact((Object)1, (Object)2, (Object)3);
        // invokeExact(Ljava/lang/Object;Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;
        assertEquals(x, java.util.Arrays.asList(1,2,3));
        // mt is ()int
        mt = MethodType.methodType(int.class);
        mh = lookup.findVirtual(java.util.List.class, "size", mt);
        i = (int) mh.invokeExact(java.util.Arrays.asList(1,2,3));
        // invokeExact(Ljava/util/List;)I
        DCHECK(i == 3);
        mt = MethodType.methodType(void.class, String.class);
        mh = lookup.findVirtual(java.io.PrintStream.class, "println", mt);
        mh.invokeExact(System.out, "Hello, world.");
        // invokeExact(Ljava/io/PrintStream;Ljava/lang/String;)V

        MethodHandle deepToString = publicLookup()
          .findStatic(Arrays.class, "deepToString", methodType(String.class, Object[].class));
        MethodHandle ts1 = deepToString.asVarargsCollector(Object[].class);
        assertEquals("[won]",   (String) ts1.invokeExact(    new Object[]{"won"}));
        assertEquals("[won]",   (String) ts1.invoke(         new Object[]{"won"}));
        assertEquals("[won]",   (String) ts1.invoke(                      "won" ));
        assertEquals("[[won]]", (String) ts1.invoke((Object) new Object[]{"won"}));
        // findStatic of Arrays.asList(...) produces a variable arity method handle:
        MethodHandle asList = publicLookup()
          .findStatic(Arrays.class, "asList", methodType(List.class, Object[].class));
        assertEquals(methodType(List.class, Object[].class), asList.type());
        DCHECK(asList.isVarargsCollector());
        assertEquals("[]", asList.invoke().toString());
        assertEquals("[1]", asList.invoke(1).toString());
        assertEquals("[two, too]", asList.invoke("two", "too").toString());
        String[] argv = { "three", "thee", "tee" };
        assertEquals("[three, thee, tee]", asList.invoke(argv).toString());
        assertEquals("[three, thee, tee]", asList.invoke((Object[])argv).toString());
        List ls = (List) asList.invoke((Object)argv);
        assertEquals(1, ls.size());
        assertEquals("[three, thee, tee]", Arrays.toString((Object[])ls.get(0)));
    }
}
