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
	});
});
