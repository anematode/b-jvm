
record TestRecord(Object inner) {}

public class Records {
    public static void main(String[] args) {
        var myInner = new Object();

        var a = new TestRecord(myInner);
        var b = new TestRecord(myInner);

        System.out.println(a.equals(b));
        System.out.println(a.hashCode() == b.hashCode());
    }
}
