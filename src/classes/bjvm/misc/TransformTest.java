package bjvm.misc;

import java.io.IOException;
import java.lang.reflect.InvocationTargetException;

public class TransformTest {
	public static void main(String[] args) throws IOException, NoSuchMethodException, InvocationTargetException, IllegalAccessException {
		var target = TransformTest.class.getClassLoader().getResourceAsStream("bjvm/misc/TestClass.class").readAllBytes();

		var transformed = MemberRewriter.rewrite(target);
		var loader = new MyClassLoader();

		var k = loader.defineClass("bjvm.misc.TestClass", transformed);
		k.getMethod("doTest").invoke(null);
	}

	private static class MyClassLoader extends ClassLoader {
		public Class<?> defineClass(String name, byte[] bytes) {
			return defineClass(name, bytes, 0, bytes.length);
		}
	}
}
