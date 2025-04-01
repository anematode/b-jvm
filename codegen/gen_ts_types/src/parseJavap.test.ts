import { parseJavap, ClassInfo, JavaType, JavaMethod } from "./parseJavap";
import dedent from "dedent";

const expectOutput = (javap: string, expected: ClassInfo[]) => {
	const result = parseJavap(javap);
	expect(result).toEqual(expected);
};

describe("parseJavap", () => {
	describe("basic class parsing", () => {
		it("should parse a simple class", () => {
			const javap = dedent`
				Compiled from "Simple.java"
				public class com.example.Simple {
					public com.example.Simple();
					public void simpleMethod();
					public int simpleField;
				}
			`;

			expectOutput(javap, [
				{
					className: "Simple",
					modifiers: ["public"],
					interfaces: [],
					methods: [
						{
							name: "Simple",
							kind: "constructor",
							returnType: {
								kind: "class",
								name: "com.example.Simple",
							},
							modifiers: ["public"],
							parameters: [],
						},
						{
							name: "simpleMethod",
							kind: "method",
							returnType: {
								kind: "primitive",
								type: "void",
							},
							modifiers: ["public"],
							parameters: [],
						},
					],
					fields: [
						{
							name: "simpleField",
							modifiers: ["public"],
							type: { kind: "primitive", type: "int" },
						},
					],
					isInterface: false,
					packageName: "com.example",
				},
			]);
		});

		it("should parse methods with parameters", () => {
			const javap = dedent`
				Compiled from "MethodParams.java"
				public class com.example.MethodParams {
					public com.example.MethodParams();
					public void simpleParam(int x);
					public java.lang.String multipleParams(int x, java.lang.String name, boolean flag);
					public java.util.List<java.lang.String> genericParam(java.util.Map<java.lang.String, java.lang.Integer> data);
					public int[] arrayParam(java.lang.String[] names);
				}
			`;

			expectOutput(javap, [
				{
					className: "MethodParams",
					modifiers: ["public"],
					interfaces: [],
					methods: [
						{
							name: "MethodParams",
							kind: "constructor",
							returnType: {
								kind: "class",
								name: "com.example.MethodParams",
							},
							modifiers: ["public"],
							parameters: [],
						},
						{
							name: "simpleParam",
							kind: "method",
							returnType: {
								kind: "primitive",
								type: "void",
							},
							modifiers: ["public"],
							parameters: [
								{
									name: "x",
									type: { kind: "primitive", type: "int" },
								},
							],
						},
						{
							name: "multipleParams",
							kind: "method",
							returnType: {
								kind: "class",
								name: "java.lang.String",
							},
							modifiers: ["public"],
							parameters: [
								{
									name: "x",
									type: { kind: "primitive", type: "int" },
								},
								{
									name: "name",
									type: {
										kind: "class",
										name: "java.lang.String",
									},
								},
								{
									name: "flag",
									type: {
										kind: "primitive",
										type: "boolean",
									},
								},
							],
						},
						{
							name: "genericParam",
							kind: "method",
							returnType: {
								kind: "class",
								name: "java.util.List<java.lang.String>",
							},
							modifiers: ["public"],
							parameters: [
								{
									name: "data",
									type: {
										kind: "class",
										name: "java.util.Map<java.lang.String, java.lang.Integer>",
									},
								},
							],
						},
						{
							name: "arrayParam",
							kind: "method",
							returnType: {
								kind: "array",
								type: { kind: "primitive", type: "int" },
								dimensions: 1,
							},
							modifiers: ["public"],
							parameters: [
								{
									name: "names",
									type: {
										kind: "array",
										type: {
											kind: "class",
											name: "java.lang.String",
										},
										dimensions: 1,
									},
								},
							],
						},
					],
					fields: [],
					isInterface: false,
					packageName: "com.example",
				},
			]);
		});

		it("should parse a class that extends another class", () => {
			const javap = dedent`
				Compiled from "Child.java"
				public class com.example.Child extends com.example.Parent {
					public com.example.Child();
					public void childMethod();
					protected int parentOverride(java.lang.String param);
				}
			`;

			expectOutput(javap, [
				{
					className: "Child",
					modifiers: ["public"],
					interfaces: [],
					superClass: "com.example.Parent",
					methods: [
						{
							name: "Child",
							kind: "constructor",
							returnType: {
								kind: "class",
								name: "com.example.Child",
							},
							modifiers: ["public"],
							parameters: [],
						},
						{
							name: "childMethod",
							kind: "method",
							returnType: {
								kind: "primitive",
								type: "void",
							},
							modifiers: ["public"],
							parameters: [],
						},
						{
							name: "parentOverride",
							kind: "method",
							returnType: {
								kind: "primitive",
								type: "int",
							},
							modifiers: ["protected"],
							parameters: [
								{
									name: "param",
									type: {
										kind: "class",
										name: "java.lang.String",
									},
								},
							],
						},
					],
					fields: [],
					isInterface: false,
					packageName: "com.example",
				},
			]);
		});

		it("should parse a class that implements an interface", () => {
			const javap = dedent`
				Compiled from "Implementation.java"
				public class com.example.Implementation implements com.example.SomeInterface {
					public com.example.Implementation();
					public void interfaceMethod(int value);
					public java.lang.String getDescription();
				}
			`;

			expectOutput(javap, [
				{
					className: "Implementation",
					modifiers: ["public"],
					interfaces: ["com.example.SomeInterface"],
					methods: [
						{
							name: "Implementation",
							kind: "constructor",
							returnType: {
								kind: "class",
								name: "com.example.Implementation",
							},
							modifiers: ["public"],
							parameters: [],
						},
						{
							name: "interfaceMethod",
							kind: "method",
							returnType: {
								kind: "primitive",
								type: "void",
							},
							modifiers: ["public"],
							parameters: [
								{
									name: "value",
									type: { kind: "primitive", type: "int" },
								},
							],
						},
						{
							name: "getDescription",
							kind: "method",
							returnType: {
								kind: "class",
								name: "java.lang.String",
							},
							modifiers: ["public"],
							parameters: [],
						},
					],
					fields: [],
					isInterface: false,
					packageName: "com.example",
				},
			]);
		});

		it("should parse a class that both extends a class and implements multiple interfaces", () => {
			const javap = dedent`
				Compiled from "FullExample.java"
				public class com.example.FullExample extends com.example.BaseClass implements com.example.Interface1, com.example.Interface2, java.io.Serializable {
					private static final long serialVersionUID;
					protected com.example.Helper helper;
					public com.example.FullExample(com.example.Helper helper);
					public void interface1Method();
					public java.lang.String interface2Method(int count);
					protected com.example.Result baseClassOverride(java.lang.String input, int flags);
					private void internalProcessing();
					public static com.example.FullExample create(com.example.Helper helper);
				}
			`;

			expectOutput(javap, [
				{
					className: "FullExample",
					modifiers: ["public"],
					interfaces: [
						"com.example.Interface1",
						"com.example.Interface2",
						"java.io.Serializable",
					],
					superClass: "com.example.BaseClass",
					methods: [
						{
							name: "FullExample",
							kind: "constructor",
							returnType: {
								kind: "class",
								name: "com.example.FullExample",
							},
							modifiers: ["public"],
							parameters: [
								{
									name: "helper",
									type: {
										kind: "class",
										name: "com.example.Helper",
									},
								},
							],
						},
						{
							name: "interface1Method",
							kind: "method",
							returnType: {
								kind: "primitive",
								type: "void",
							},
							modifiers: ["public"],
							parameters: [],
						},
						{
							name: "interface2Method",
							kind: "method",
							returnType: {
								kind: "class",
								name: "java.lang.String",
							},
							modifiers: ["public"],
							parameters: [
								{
									name: "count",
									type: { kind: "primitive", type: "int" },
								},
							],
						},
						{
							name: "baseClassOverride",
							kind: "method",
							returnType: {
								kind: "class",
								name: "com.example.Result",
							},
							modifiers: ["protected"],
							parameters: [
								{
									name: "input",
									type: {
										kind: "class",
										name: "java.lang.String",
									},
								},
								{
									name: "flags",
									type: { kind: "primitive", type: "int" },
								},
							],
						},
						{
							name: "internalProcessing",
							kind: "method",
							returnType: {
								kind: "primitive",
								type: "void",
							},
							modifiers: ["private"],
							parameters: [],
						},
						{
							name: "create",
							kind: "method",
							returnType: {
								kind: "class",
								name: "com.example.FullExample",
							},
							modifiers: ["public", "static"],
							parameters: [
								{
									name: "helper",
									type: {
										kind: "class",
										name: "com.example.Helper",
									},
								},
							],
						},
					],
					fields: [
						{
							name: "serialVersionUID",
							modifiers: ["private", "static", "final"],
							type: { kind: "primitive", type: "long" },
						},
						{
							name: "helper",
							modifiers: ["protected"],
							type: { kind: "class", name: "com.example.Helper" },
						},
					],
					isInterface: false,
					packageName: "com.example",
				},
			]);
		});
	});

	describe("interface parsing", () => {
		it("should parse a simple interface", () => {
			const javap = dedent`
				Compiled from "SimpleInterface.java"
				public interface com.example.SimpleInterface {
					public abstract void interfaceMethod();
					public abstract java.lang.String getName(int id);
					public static final int CONSTANT_VALUE = 100;
				}
			`;

			expectOutput(javap, [
				{
					className: "SimpleInterface",
					modifiers: ["public"],
					interfaces: [],
					methods: [
						{
							name: "interfaceMethod",
							kind: "method",
							returnType: {
								kind: "primitive",
								type: "void",
							},
							modifiers: ["public", "abstract"],
							parameters: [],
						},
						{
							name: "getName",
							kind: "method",
							returnType: {
								kind: "class",
								name: "java.lang.String",
							},
							modifiers: ["public", "abstract"],
							parameters: [
								{
									name: "id",
									type: { kind: "primitive", type: "int" },
								},
							],
						},
					],
					fields: [
						{
							name: "CONSTANT_VALUE",
							modifiers: ["public", "static", "final"],
							type: { kind: "primitive", type: "int" },
						},
					],
					isInterface: true,
					packageName: "com.example",
				},
			]);
		});

		it("should parse an interface that extends other interfaces", () => {
			const javap = dedent`
				Compiled from "ExtendingInterface.java"
				public interface com.example.ExtendingInterface extends com.example.BaseInterface1, com.example.BaseInterface2 {
					public abstract void method1(java.lang.String input);
					public abstract int method2(java.util.List<java.lang.String> items);
					public static final java.lang.String VERSION = "1.0";
				}
			`;

			expectOutput(javap, [
				{
					className: "ExtendingInterface",
					modifiers: ["public"],
					interfaces: [
						"com.example.BaseInterface1",
						"com.example.BaseInterface2",
					],
					methods: [
						{
							name: "method1",
							kind: "method",
							returnType: {
								kind: "primitive",
								type: "void",
							},
							modifiers: ["public", "abstract"],
							parameters: [
								{
									name: "input",
									type: {
										kind: "class",
										name: "java.lang.String",
									},
								},
							],
						},
						{
							name: "method2",
							kind: "method",
							returnType: {
								kind: "primitive",
								type: "int",
							},
							modifiers: ["public", "abstract"],
							parameters: [
								{
									name: "items",
									type: {
										kind: "class",
										name: "java.util.List<java.lang.String>",
									},
								},
							],
						},
					],
					fields: [
						{
							name: "VERSION",
							modifiers: ["public", "static", "final"],
							type: { kind: "class", name: "java.lang.String" },
						},
					],
					isInterface: true,
					packageName: "com.example",
				},
			]);
		});
	});

	describe("common real-world cases", () => {
		it("should parse methods with throws declarations", () => {
			const javap = dedent`
				Compiled from "ExceptionThrower.java"
				public class com.example.ExceptionThrower {
					public com.example.ExceptionThrower();
					public void methodThrowsOne() throws java.io.IOException;
					public java.lang.String methodThrowsMultiple(int value) throws java.io.IOException, java.lang.IllegalArgumentException;
					protected void handleWithoutThrowing(java.lang.String input);
				}
			`;

			expectOutput(javap, [
				{
					className: "ExceptionThrower",
					modifiers: ["public"],
					interfaces: [],
					methods: [
						{
							name: "ExceptionThrower",
							kind: "constructor",
							returnType: {
								kind: "class",
								name: "com.example.ExceptionThrower",
							},
							modifiers: ["public"],
							parameters: [],
						},
						{
							name: "methodThrowsOne",
							kind: "method",
							returnType: {
								kind: "primitive",
								type: "void",
							},
							modifiers: ["public"],
							parameters: [],
							throws: ["java.io.IOException"],
						},
						{
							name: "methodThrowsMultiple",
							kind: "method",
							returnType: {
								kind: "class",
								name: "java.lang.String",
							},
							modifiers: ["public"],
							parameters: [
								{
									name: "value",
									type: { kind: "primitive", type: "int" },
								},
							],
							throws: [
								"java.io.IOException",
								"java.lang.IllegalArgumentException",
							],
						},
						{
							name: "handleWithoutThrowing",
							kind: "method",
							returnType: {
								kind: "primitive",
								type: "void",
							},
							modifiers: ["protected"],
							parameters: [
								{
									name: "input",
									type: {
										kind: "class",
										name: "java.lang.String",
									},
								},
							],
						},
					],
					fields: [],
					isInterface: false,
					packageName: "com.example",
				},
			]);
		});

		it("should parse methods with varargs parameters", () => {
			const javap = dedent`
					Compiled from "VarargsExample.java"
					public class com.example.VarargsExample {
						public com.example.VarargsExample();
						public void simpleVarargs(java.lang.String... messages);
						public <T> T[] combineArrays(T... elements);
						public void mixedParams(int count, java.lang.String name, java.lang.Object... items);
					}
				`;

			expectOutput(javap, [
				{
					className: "VarargsExample",
					modifiers: ["public"],
					interfaces: [],
					methods: [
						{
							name: "VarargsExample",
							kind: "constructor",
							returnType: {
								kind: "class",
								name: "com.example.VarargsExample",
							},
							modifiers: ["public"],
							parameters: [],
						},
						{
							name: "simpleVarargs",
							kind: "method",
							returnType: {
								kind: "primitive",
								type: "void",
							},
							modifiers: ["public"],
							parameters: [
								{
									name: "messages",
									isVarargs: true,
									type: {
										kind: "array",
										type: {
											kind: "class",
											name: "java.lang.String",
										},
										dimensions: 1,
									},
								},
							],
						},
						{
							name: "combineArrays",
							kind: "method",
							returnType: {
								kind: "array",
								type: { kind: "class", name: "T" },
								dimensions: 1,
							},
							modifiers: ["public"],
							parameters: [
								{
									name: "elements",
									isVarargs: true,
									type: {
										kind: "array",
										type: { kind: "class", name: "T" },
										dimensions: 1,
									},
								},
							],
							typeParameters: [{ name: "T" }],
						},
						{
							name: "mixedParams",
							kind: "method",
							returnType: {
								kind: "primitive",
								type: "void",
							},
							modifiers: ["public"],
							parameters: [
								{
									name: "count",
									type: { kind: "primitive", type: "int" },
								},
								{
									name: "name",
									type: {
										kind: "class",
										name: "java.lang.String",
									},
								},
								{
									name: "items",
									isVarargs: true,
									type: {
										kind: "array",
										type: {
											kind: "class",
											name: "java.lang.Object",
										},
										dimensions: 1,
									},
								},
							],
						},
					],
					fields: [],
					isInterface: false,
					packageName: "com.example",
				},
			]);
		});

		it("should parse generic classes and methods with type parameters", () => {
			const javap = dedent`
				Compiled from "GenericExample.java"
				public class com.example.GenericExample<T, R extends java.io.Serializable> {
					public com.example.GenericExample();
					public T getFirst();
					public <U> U transform(T value, java.util.function.Function<T, U> transformer);
					public <K, V extends java.lang.Comparable<V>> java.util.Map<K, V> createMap(K key, V value);
					public <E extends java.lang.Exception> void withException() throws E;
					public java.util.List<R> getItems();
				}
			`;

			expectOutput(javap, [
				{
					className: "GenericExample",
					modifiers: ["public"],
					interfaces: [],
					typeParameters: [
						{ name: "T" },
						{ name: "R", bounds: ["java.io.Serializable"] },
					],
					methods: [
						{
							name: "GenericExample",
							kind: "constructor",
							returnType: {
								kind: "class",
								name: "com.example.GenericExample",
							},
							modifiers: ["public"],
							parameters: [],
						},
						{
							name: "getFirst",
							kind: "method",
							returnType: {
								kind: "class",
								name: "T",
							},
							modifiers: ["public"],
							parameters: [],
						},
						{
							name: "transform",
							kind: "method",
							returnType: {
								kind: "class",
								name: "U",
							},
							modifiers: ["public"],
							parameters: [
								{
									name: "value",
									type: {
										kind: "class",
										name: "T",
									},
								},
								{
									name: "transformer",
									type: {
										kind: "class",
										name: "java.util.function.Function<T, U>",
									},
								},
							],
							typeParameters: [{ name: "U" }],
						},
						{
							name: "createMap",
							kind: "method",
							returnType: {
								kind: "class",
								name: "java.util.Map<K, V>",
							},
							modifiers: ["public"],
							parameters: [
								{
									name: "key",
									type: {
										kind: "class",
										name: "K",
									},
								},
								{
									name: "value",
									type: {
										kind: "class",
										name: "V",
									},
								},
							],
							typeParameters: [
								{ name: "K" },
								{
									name: "V",
									bounds: ["java.lang.Comparable<V>"],
								},
							],
						},
						{
							name: "withException",
							kind: "method",
							returnType: {
								kind: "primitive",
								type: "void",
							},
							modifiers: ["public"],
							parameters: [],
							throws: ["E"],
							typeParameters: [
								{ name: "E", bounds: ["java.lang.Exception"] },
							],
						},
						{
							name: "getItems",
							kind: "method",
							returnType: {
								kind: "class",
								name: "java.util.List<R>",
							},
							modifiers: ["public"],
							parameters: [],
						},
					],
					fields: [],
					isInterface: false,
					packageName: "com.example",
				},
			]);
		});

		it("should parse complex nested generic types", () => {
			const javap = dedent`
				Compiled from "ComplexGenerics.java"
				public class com.example.ComplexGenerics {
					public com.example.ComplexGenerics();
					public java.util.Map<java.lang.String, java.util.List<java.lang.Integer>> getNestedMap();
					public void processNested(java.util.List<java.util.Map<java.lang.String, java.lang.Double>> dataList);
					public <T, R> java.util.Map<R, java.util.List<T>> transformData(java.util.List<T> items, java.util.function.Function<T, R> transformer);
					private java.util.HashMap<java.lang.String, java.util.concurrent.Future<java.util.Optional<java.lang.Object>>> deeplyNestedField;
				}
			`;

			expectOutput(javap, [
				{
					className: "ComplexGenerics",
					modifiers: ["public"],
					interfaces: [],
					methods: [
						{
							name: "ComplexGenerics",
							kind: "constructor",
							returnType: {
								kind: "class",
								name: "com.example.ComplexGenerics",
							},
							modifiers: ["public"],
							parameters: [],
						},
						{
							name: "getNestedMap",
							kind: "method",
							returnType: {
								kind: "class",
								name: "java.util.Map<java.lang.String, java.util.List<java.lang.Integer>>",
							},
							modifiers: ["public"],
							parameters: [],
						},
						{
							name: "processNested",
							kind: "method",
							returnType: {
								kind: "primitive",
								type: "void",
							},
							modifiers: ["public"],
							parameters: [
								{
									name: "dataList",
									type: {
										kind: "class",
										name: "java.util.List<java.util.Map<java.lang.String, java.lang.Double>>",
									},
								},
							],
						},
						{
							name: "transformData",
							kind: "method",
							returnType: {
								kind: "class",
								name: "java.util.Map<R, java.util.List<T>>",
							},
							modifiers: ["public"],
							parameters: [
								{
									name: "items",
									type: {
										kind: "class",
										name: "java.util.List<T>",
									},
								},
								{
									name: "transformer",
									type: {
										kind: "class",
										name: "java.util.function.Function<T, R>",
									},
								},
							],
							typeParameters: [{ name: "T" }, { name: "R" }],
						},
					],
					fields: [
						{
							name: "deeplyNestedField",
							type: {
								kind: "class",
								name: "java.util.HashMap<java.lang.String, java.util.concurrent.Future<java.util.Optional<java.lang.Object>>>",
							},
							modifiers: ["private"],
						},
					],
					isInterface: false,
					packageName: "com.example",
				},
			]);
		});
	});

	describe("descriptor handling", () => {
		it("should preserve non-empty descriptors", () => {
			const javap = dedent`
				Compiled from "DescriptorExample.java"
				public class com.example.DescriptorExample {
					public com.example.DescriptorExample(); // constructor descriptor
					public void methodWithDescriptor(int value); // method descriptor
					private java.lang.String fieldWithComment; // field descriptor
				}
			`;

			expectOutput(javap, [
				{
					className: "DescriptorExample",
					modifiers: ["public"],
					interfaces: [],
					methods: [
						{
							name: "DescriptorExample",
							kind: "constructor",
							returnType: {
								kind: "class",
								name: "com.example.DescriptorExample",
							},
							modifiers: ["public"],
							parameters: [],
							descriptor: "constructor descriptor",
						},
						{
							name: "methodWithDescriptor",
							kind: "method",
							returnType: {
								kind: "primitive",
								type: "void",
							},
							modifiers: ["public"],
							parameters: [
								{
									name: "value",
									type: { kind: "primitive", type: "int" },
								},
							],
							descriptor: "method descriptor",
						},
					],
					fields: [
						{
							name: "fieldWithComment",
							modifiers: ["private"],
							type: { kind: "class", name: "java.lang.String" },
							descriptor: "field descriptor",
						},
					],
					isInterface: false,
					packageName: "com.example",
				},
			]);
		});

		it("should omit descriptors when they don't exist", () => {
			const javap = dedent`
				Compiled from "NoDescriptorExample.java"
				public class com.example.NoDescriptorExample {
					public com.example.NoDescriptorExample();
					public void methodWithoutDescriptor(int value);
					private java.lang.String fieldWithoutComment;
				}
			`;

			expectOutput(javap, [
				{
					className: "NoDescriptorExample",
					modifiers: ["public"],
					interfaces: [],
					methods: [
						{
							name: "NoDescriptorExample",
							kind: "constructor",
							returnType: {
								kind: "class",
								name: "com.example.NoDescriptorExample",
							},
							modifiers: ["public"],
							parameters: [],
						},
						{
							name: "methodWithoutDescriptor",
							kind: "method",
							returnType: {
								kind: "primitive",
								type: "void",
							},
							modifiers: ["public"],
							parameters: [
								{
									name: "value",
									type: { kind: "primitive", type: "int" },
								},
							],
						},
					],
					fields: [
						{
							name: "fieldWithoutComment",
							modifiers: ["private"],
							type: { kind: "class", name: "java.lang.String" },
						},
					],
					isInterface: false,
					packageName: "com.example",
				},
			]);
		});
	});
});
