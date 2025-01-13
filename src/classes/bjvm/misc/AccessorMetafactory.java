package bjvm.misc;

import java.lang.constant.ClassDesc;
import java.lang.constant.ConstantDescs;
import java.lang.constant.DirectMethodHandleDesc;
import java.lang.invoke.CallSite;
import java.lang.invoke.ConstantCallSite;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;

public class AccessorMetafactory {
	public static final DirectMethodHandleDesc GETTER_FACTORY = ConstantDescs.ofCallsiteBootstrap(
			ClassDesc.of("bjvm.misc.AccessorMetafactory"),
			"getterMetafactory",
			ClassDesc.of("java.lang.invoke.CallSite"),
			ClassDesc.of("java.lang.String"), ClassDesc.of("java.lang.String")
	);

	public static final DirectMethodHandleDesc SETTER_FACTORY = ConstantDescs.ofCallsiteBootstrap(
			ClassDesc.of("bjvm.misc.AccessorMetafactory"),
			"setterMetafactory",
			ClassDesc.of("java.lang.invoke.CallSite"),
			ClassDesc.of("java.lang.String"), ClassDesc.of("java.lang.String")
	);

	public static CallSite getterMetafactory(MethodHandles.Lookup lookup, String name, MethodType mt, String className, String fieldName) throws Throwable {
		var klass = lookup.findClass(className);
		return new ConstantCallSite(lookup.findGetter(klass, fieldName, mt.returnType()));
	}

	public static CallSite setterMetafactory(MethodHandles.Lookup lookup, String name, MethodType mt, String className, String fieldName) throws Throwable {
		var klass = lookup.findClass(className);
		return new ConstantCallSite(lookup.findSetter(klass, fieldName, mt.parameterType(1)));
	}
}
