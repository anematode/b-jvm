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
							descriptor: "",
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
							descriptor: "",
						},
					],
					fields: [
						{
							name: "simpleField",
							modifiers: ["public"],
							type: { kind: "primitive", type: "int" },
							descriptor: "",
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
							descriptor: "",
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
							descriptor: "",
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
							descriptor: "",
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
							descriptor: "",
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
							descriptor: "",
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
							descriptor: "",
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
							descriptor: "",
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
							descriptor: "",
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
							descriptor: "",
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
							descriptor: "",
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
							descriptor: "",
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
							descriptor: "",
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
							descriptor: "",
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
							descriptor: "",
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
							descriptor: "",
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
							descriptor: "",
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
							descriptor: "",
						},
					],
					fields: [
						{
							name: "serialVersionUID",
							modifiers: ["private", "static", "final"],
							type: { kind: "primitive", type: "long" },
							descriptor: "",
						},
						{
							name: "helper",
							modifiers: ["protected"],
							type: { kind: "class", name: "com.example.Helper" },
							descriptor: "",
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
							descriptor: "",
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
							descriptor: "",
						},
					],
					fields: [
						{
							name: "CONSTANT_VALUE",
							modifiers: ["public", "static", "final"],
							type: { kind: "primitive", type: "int" },
							descriptor: "",
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
							descriptor: "",
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
							descriptor: "",
						},
					],
					fields: [
						{
							name: "VERSION",
							modifiers: ["public", "static", "final"],
							type: { kind: "class", name: "java.lang.String" },
							descriptor: "",
						},
					],
					isInterface: true,
					packageName: "com.example",
				},
			]);
		});
	});
});
