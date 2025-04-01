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
				}
			`;

			expectOutput(javap, [
				{
					className: "Simple",
					packageName: "com.example",
					isInterface: false,
					modifiers: ["public"],
					superClass: undefined,
					interfaces: [],
					methods: [
						{
							name: "simpleMethod",
							kind: "method",
							modifiers: ["public"],
							returnType: { kind: "primitive", type: "void" },
							parameters: [],
							descriptor: "()V",
						},
					],
					fields: [],
				},
			]);
		});

		it("should parse class with superclass and interfaces", () => {
			const javap = dedent`
				Compiled from "Extended.java"
				public class com.example.Extended extends com.example.Base implements java.io.Serializable, java.lang.Comparable {
					public com.example.Extended();
				}
			`;

			expectOutput(javap, [
				{
					className: "Extended",
					packageName: "com.example",
					isInterface: false,
					modifiers: ["public"],
					superClass: "com.example.Base",
					interfaces: [
						"java.io.Serializable",
						"java.lang.Comparable",
					],
					methods: [],
					fields: [],
				},
			]);
		});
	});

	describe("interface parsing", () => {
		it("should parse a simple interface", () => {
			const javap = dedent`
				Compiled from "MyInterface.java"
				public interface com.example.MyInterface {
					public abstract void interfaceMethod();
				}
			`;

			expectOutput(javap, [
				{
					className: "MyInterface",
					packageName: "com.example",
					isInterface: true,
					modifiers: ["public"],
					superClass: undefined,
					interfaces: [],
					methods: [],
					fields: [],
				},
			]);
		});

		it("should parse interface with extended interfaces", () => {
			const javap = dedent`
       			Compiled from "ExtendedInterface.java"
				public interface com.example.ExtendedInterface extends java.util.List, java.lang.Comparable {
					public abstract void methodOne();
				}
			`;

			expectOutput(javap, [
				{
					className: "ExtendedInterface",
					packageName: "com.example",
					isInterface: true,
					modifiers: ["public"],
					superClass: undefined,
					interfaces: ["java.util.List", "java.lang.Comparable"],
					methods: [],
					fields: [],
				},
			]);
		});
	});

	describe("method parsing", () => {
		it("should parse regular methods", () => {
			const javap = dedent`
				Compiled from "Methods.java"
				public class com.example.Methods {
					public void simpleMethod();
					protected static final int complexMethod(java.lang.String, int) throws java.lang.Exception;
				}
			`;

			expectOutput(javap, [
				{
					className: "Methods",
					packageName: "com.example",
					isInterface: false,
					modifiers: ["public"],
					superClass: undefined,
					interfaces: [],
					methods: [
						{
							name: "simpleMethod",
							kind: "method",
							modifiers: ["public"],
							returnType: { kind: "primitive", type: "void" },
							parameters: [],
							descriptor: "()V",
						},
						{
							name: "complexMethod",
							kind: "method",
							modifiers: ["protected", "static", "final"],
							returnType: { kind: "primitive", type: "int" },
							parameters: [
								{
									type: {
										kind: "class",
										name: "java.lang.String",
									},
								},
								{ type: { kind: "primitive", type: "int" } },
							],
							descriptor: "(Ljava/lang/String;I)I",
						},
					],
					fields: [],
				},
			]);
		});

		it("should parse constructors", () => {
			const javap = dedent`
				Compiled from "Constructors.java"
				public class com.example.Constructors {
					public com.example.Constructors();
					public com.example.Constructors(int, java.lang.String);
				}
			`;

			expectOutput(javap, [
				{
					className: "Constructors",
					packageName: "com.example",
					isInterface: false,
					modifiers: ["public"],
					superClass: undefined,
					interfaces: [],
					methods: [
						{
							name: "simpleMethod",
							kind: "method",
							modifiers: ["public"],
							returnType: {
								kind: "class",
								name: "com.example.Constructors",
							},
							parameters: [],
							descriptor: "()V",
						},
						{
							name: "complexMethod",
							kind: "method",
							modifiers: ["protected", "static", "final"],
							returnType: { kind: "primitive", type: "int" },
							parameters: [
								{
									type: {
										kind: "class",
										name: "java.lang.String",
									},
								},
								{ type: { kind: "primitive", type: "int" } },
							],
							descriptor: "(Ljava/lang/String;I)I",
						},
					],
					fields: [],
				},
			]);
		});

		it("should parse generic types in methods", () => {
			const javap = dedent`
				Compiled from "Generics.java"
				public class com.example.Generics {
					public void setMap(java.util.HashMap<java.lang.String, java.util.LinkedList<java.lang.Integer>>);
					public java.util.List<java.lang.String> getNames();
				}
			`;

			expectOutput(javap, [
				{
					className: "Generics",
					packageName: "com.example",
					isInterface: false,
					modifiers: ["public"],
					superClass: undefined,
					interfaces: [],
					methods: [
						{
							name: "setMap",
							kind: "method",
							modifiers: ["public"],
							returnType: { kind: "class", name: "void" },
							parameters: [
								{
									type: {
										kind: "class",
										name: "java.util.HashMap<java.lang.String, java.util.LinkedList<java.lang.Integer>>",
									},
								},
							],
							descriptor:
								"(Ljava/util/HashMap<Ljava/lang/String;Ljava/util/LinkedList<Ljava/lang/Integer;>;)V",
						},
						{
							name: "getNames",
							kind: "method",
							modifiers: ["public"],
							returnType: {
								kind: "class",
								name: "java.util.List<java.lang.String>",
							},
							parameters: [],
							descriptor:
								"()Ljava/util/List<Ljava/lang/String;>;",
						},
					],
					fields: [],
				},
			]);
		});

		it("should parse constructors", () => {
			const javap = dedent`
				Compiled from "Constructors.java"
				public class com.example.Constructors {
					public com.example.Constructors();
					public com.example.Constructors(int, java.lang.String);
				}
			`;

			expectOutput(javap, [
				{
					className: "Constructors",
					packageName: "com.example",
					isInterface: false,
					modifiers: ["public"],
					superClass: undefined,
					interfaces: [],
					methods: [
						{
							name: "simpleMethod",
							kind: "method",
							modifiers: ["public"],
							returnType: { kind: "primitive", type: "void" },
							parameters: [],
							descriptor: "()V",
						},
						{
							name: "complexMethod",
							kind: "method",
							modifiers: ["protected", "static", "final"],
							returnType: { kind: "primitive", type: "int" },
							parameters: [
								{
									type: {
										kind: "class",
										name: "java.lang.String",
									},
								},
								{ type: { kind: "primitive", type: "int" } },
							],
							descriptor: "(Ljava/lang/String;I)I",
						},
					],
					fields: [],
				},
			]);
		});
	});

	describe("field parsing", () => {
		it("should parse fields", () => {
			const javap = dedent`
				Compiled from "Fields.java"
				public class com.example.Fields {
					private int count;
					public static final java.lang.String NAME;
					protected volatile java.util.List<java.lang.String> items;
				}
			`;

			expectOutput(javap, [
				{
					className: "Fields",
					packageName: "com.example",
					isInterface: false,
					modifiers: ["public"],
					superClass: undefined,
					interfaces: [],
					methods: [],
					fields: [
						{
							name: "count",
							modifiers: ["private"],
							type: { kind: "primitive", type: "int" },
							descriptor: "I",
						},
						{
							name: "NAME",
							modifiers: ["public", "static", "final"],
							type: { kind: "class", name: "java.lang.String" },
							descriptor: "Ljava/lang/String;",
						},
						{
							name: "items",
							modifiers: ["protected", "volatile"],
							type: {
								kind: "class",
								name: "java.util.List<java.lang.String>",
							},
							descriptor: "Ljava/util/List<Ljava/lang/String;>;",
						},
					],
				},
			]);
		});

		it("should parse array types", () => {
			const javap = dedent`
				Compiled from "Arrays.java"
				public class com.example.Arrays {
					public byte[] getBytes();
					public java.lang.String[][] getMatrix();
					private int[][][] cube;
				}
			`;

			expectOutput(javap, [
				{
					className: "Arrays",
					packageName: "com.example",
					isInterface: false,
					modifiers: ["public"],
					superClass: undefined,
					interfaces: [],
					methods: [
						{
							name: "getBytes",
							kind: "method",
							modifiers: ["public"],
							returnType: {
								kind: "array",
								type: { kind: "primitive", type: "byte" },
								dimensions: 1,
							},
							parameters: [],
							descriptor: "()[B",
						},
						{
							name: "getMatrix",
							kind: "method",
							modifiers: ["public"],
							returnType: {
								kind: "array",
								type: {
									kind: "class",
									name: "java.lang.String",
								},
								dimensions: 2,
							},
							parameters: [],
							descriptor: "()[[Ljava/lang/String;",
						},
					],
					fields: [
						{
							name: "cube",
							modifiers: ["private"],
							type: {
								kind: "array",
								type: { kind: "primitive", type: "int" },
								dimensions: 3,
							},
							descriptor: "[[[I",
						},
					],
				},
			]);
		});
	});

	describe("inner class handling", () => {
		it("should handle inner class constructors", () => {
			const javap = dedent`
				Compiled from "Outer.java"
				public class com.example.Outer$Inner {
					final com.example.Outer this$0;
					public com.example.Outer$Inner(com.example.Outer);
				}
			`;

			expectOutput(javap, [
				{
					className: "Inner",
					packageName: "com.example.Outer$",
					isInterface: false,
					modifiers: ["public"],
					superClass: undefined,
					interfaces: [],
					methods: [
						{
							name: "Inner",
							kind: "constructor",
							modifiers: ["public"],
							returnType: {
								kind: "class",
								name: "com.example.Outer$Inner",
							},
							parameters: [
								{
									type: {
										kind: "class",
										name: "com.example.Outer",
									},
								},
							],
							descriptor: "(Lcom/example/Outer;)V",
						},
					],
					fields: [],
				},
			]);
		});

		it("should parse constructors", () => {
			const javap = dedent`
				Compiled from "Constructors.java"
				public class com.example.Constructors {
					public com.example.Constructors();
					public com.example.Constructors(int, java.lang.String);
				}
			`;

			expectOutput(javap, [
				{
					className: "Constructors",
					packageName: "com.example",
					isInterface: false,
					modifiers: ["public"],
					superClass: undefined,
					interfaces: [],
					methods: [],
					fields: [],
				},
			]);
		});
	});

	describe("multiple classes", () => {
		it("should parse multiple classes in one output", () => {
			const javap = dedent`
				Compiled from "First.java"
				public class com.example.First {
					public void firstMethod();
				}
				Compiled from "Second.java"
				public class com.example.Second {
					public void secondMethod();
				}
			`;

			expectOutput(javap, [
				{
					className: "First",
					packageName: "com.example",
					isInterface: false,
					modifiers: ["public"],
					superClass: undefined,
					interfaces: [],
					methods: [],
					fields: [],
				},
				{
					className: "Second",
					packageName: "com.example",
					isInterface: false,
					modifiers: ["public"],
					superClass: undefined,
					interfaces: [],
					methods: [],
					fields: [],
				},
			]);
		});
	});

	describe("generic types in methods", () => {
		it("should parse generic types in methods", () => {
			const javap = dedent`
				Compiled from "Generics.java"
				public class com.example.Generics {
					public void setMap(java.util.HashMap<java.lang.String, java.util.LinkedList<java.lang.Integer>>);
					public java.util.List<java.lang.String> getNames();
				}
			`;

			expectOutput(javap, [
				{
					className: "Generics",
					packageName: "com.example",
					isInterface: false,
					modifiers: ["public"],
					superClass: undefined,
					interfaces: [],
					methods: [
						{
							name: "setMap",
							kind: "method",
							modifiers: ["public"],
							returnType: {
								kind: "class",
								name: "void",
							},
							parameters: [
								{
									type: {
										kind: "class",
										name: "java.util.HashMap<java.lang.String, java.util.LinkedList<java.lang.Integer>>",
									},
								},
							],
							descriptor:
								"(Ljava/util/HashMap<Ljava/lang/String;Ljava/util/LinkedList<Ljava/lang/Integer;>;)V",
						},
						{
							name: "getNames",
							kind: "method",
							modifiers: ["public"],
							returnType: {
								kind: "class",
								name: "java.util.List<java.lang.String>",
							},
							parameters: [],
							descriptor:
								"()Ljava/util/List<Ljava/lang/String;>;",
						},
					],
					fields: [],
				},
			]);
		});
	});

	describe("generic types in methods", () => {
		it("should parse generic types in methods", () => {
			const javap = dedent`
				Compiled from "Generics.java"
				public class com.example.Generics {
					public void setMap(java.util.HashMap<java.lang.String, java.util.LinkedList<java.lang.Integer>>);
					public java.util.List<java.lang.String> getNames();
				}
      		`;

			expectOutput(javap, [
				{
					className: "Generics",
					packageName: "com.example",
					isInterface: false,
					modifiers: ["public"],
					superClass: undefined,
					interfaces: [],
					methods: [
						{
							name: "setMap",
							kind: "method",
							modifiers: ["public"],
							returnType: {
								kind: "class",
								name: "void",
							},
							parameters: [
								{
									type: {
										kind: "class",
										name: "java.util.HashMap<java.lang.String, java.util.LinkedList<java.lang.Integer>>",
									},
								},
							],
							descriptor:
								"(Ljava/util/HashMap<Ljava/lang/String;Ljava/util/LinkedList<Ljava/lang/Integer;>;)V",
						},
						{
							name: "getNames",
							kind: "method",
							modifiers: ["public"],
							returnType: {
								kind: "class",
								name: "java.util.List<java.lang.String>",
							},
							parameters: [],
							descriptor:
								"()Ljava/util/List<Ljava/lang/String;>;",
						},
					],
					fields: [],
				},
			]);
		});
	});

	describe("field parsing", () => {
		it("should parse fields", () => {
			const javap = dedent`
				Compiled from "Fields.java"
				public class com.example.Fields {
					private int count;
					public static final java.lang.String NAME;
					protected volatile java.util.List<java.lang.String> items;
				}
			`;

			expectOutput(javap, [
				{
					className: "Fields",
					packageName: "com.example",
					isInterface: false,
					modifiers: ["public"],
					superClass: undefined,
					interfaces: [],
					methods: [],
					fields: [
						{
							name: "count",
							modifiers: ["private"],
							type: { kind: "primitive", type: "int" },
							descriptor: "I",
						},
						{
							name: "NAME",
							modifiers: ["public", "static", "final"],
							type: { kind: "class", name: "java.lang.String" },
							descriptor: "Ljava/lang/String;",
						},
						{
							name: "items",
							modifiers: ["protected", "volatile"],
							type: {
								kind: "class",
								name: "java.util.List<java.lang.String>",
							},
							descriptor: "Ljava/util/List<Ljava/lang/String;>;",
						},
					],
				},
			]);
		});
	});
});
