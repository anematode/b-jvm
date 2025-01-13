package bjvm.misc;

public class TestClass {
	int field;

	void incrementField() {
		field++;
	}

	int getField() {
		return field;
	}

	public static void doTest() {
		var instance = new TestClass();
		System.out.println("hello");
		instance.incrementField();
		System.out.println(instance.getField());
	}
}
