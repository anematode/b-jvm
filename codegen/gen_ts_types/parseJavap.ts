export type JavaPrimitiveType =
	| "double"
	| "int"
	| "boolean"
	| "float"
	| "long"
	| "short"
	| "byte"
	| "char"
	| "void";

export type JavaType =
	| {
			kind: "primitive";
			type: JavaPrimitiveType;
	  }
	| {
			kind: "class";
			name: string;
	  }
	| {
			kind: "array";
			type: JavaType;
			dimensions: number;
	  };

export type JavaModifier =
	| "public"
	| "private"
	| "protected"
	| "static"
	| "final"
	| "abstract"
	| "synchronized"
	| "volatile"
	| "transient";

export type JavaMethodParameter = {
	type: JavaType;
	name?: string;
};

export type JavaMethodKind = "constructor" | "method";

export type JavaMethod = {
	name: string;
	kind: JavaMethodKind;
	modifiers: JavaModifier[];
	returnType: JavaType;
	parameters: JavaMethodParameter[];
	descriptor: string;
};

export type JavaField = {
	name: string;
	modifiers: JavaModifier[];
	type: JavaType;
	descriptor: string;
};

export type ClassInfo = {
	className: string;
	packageName?: string;
	modifiers: JavaModifier[];
	superClass?: string;
	interfaces: string[];
	methods: JavaMethod[];
	fields: JavaField[];
};

function parseJavaType(typeStr: string): JavaType {
	// Handle array types
	const arrayMatch = typeStr.match(/^(.+?)(\[\])+$/);
	if (arrayMatch) {
		const [, baseType, arrayBrackets] = arrayMatch;
		return {
			kind: "array",
			type: parseJavaType(baseType),
			dimensions: arrayBrackets.length / 2,
		};
	}

	// Handle primitive types
	const primitiveTypes: JavaPrimitiveType[] = [
		"double",
		"int",
		"boolean",
		"float",
		"long",
		"short",
		"byte",
		"char",
		"void",
	];

	if (primitiveTypes.includes(typeStr as JavaPrimitiveType)) {
		return {
			kind: "primitive",
			type: typeStr as JavaPrimitiveType,
		};
	}

	// Everything else is a class type
	return {
		kind: "class",
		name: typeStr,
	};
}

function parseParameters(paramsStr: string): JavaMethodParameter[] {
	if (!paramsStr.trim()) return [];

	return paramsStr
		.split(",")
		.filter(Boolean)
		.map((param) => {
			const parts = param.trim().split(/\s+/);
			return parts.length > 1
				? { type: parseJavaType(parts[0]), name: parts[1] }
				: { type: parseJavaType(parts[0]) };
		});
}

/**
 * Parses a string of javap output and returns a structured representation
 */
export function parseJavap(javapOutputString: string): ClassInfo[] {
	const lines = javapOutputString.split("\n").map((l) => l.trim());
	const makeNewResult = (): ClassInfo => ({
		className: "",
		modifiers: [],
		interfaces: [],
		methods: [],
		fields: [],
	});
	let result: ClassInfo = null as never;
	const results: ClassInfo[] = [];

	let currentSection: "class" | "fields" | "methods" = "class";
	let currentClassName = ""; // Store class name for constructor detection

	for (const line of lines) {
		if (line === "") {
			continue;
		}
		if (line.startsWith("Compiled from")) {
			result = makeNewResult();
			results.push(result);
			continue;
		}

		if (currentSection === "class" && line.includes("class")) {
			const classMatch = line.match(
				/^((?:public |final |abstract )*)?class\s+(\S+)\s+(?:extends\s+(\S+)\s+)?(?:implements\s+(\S+)\s+)?\{/
			);
			if (classMatch) {
				const [
					,
					modifiersStr,
					fullClassName,
					superClass,
					interfacesStr,
				] = classMatch;

				// Handle package and class name
				const lastDotIndex = fullClassName.lastIndexOf(".");
				if (lastDotIndex !== -1) {
					result.packageName = fullClassName.substring(
						0,
						lastDotIndex
					);
					result.className = fullClassName.substring(
						lastDotIndex + 1
					);
				} else {
					result.className = fullClassName;
				}
				currentClassName = result.className; // Store for constructor detection

				// Handle modifiers
				if (modifiersStr) {
					result.modifiers = modifiersStr
						.trim()
						.split(" ")
						.filter(Boolean) as JavaModifier[];
				}

				// Handle superclass
				if (superClass) {
					result.superClass = superClass;
				}

				// Handle interfaces
				if (interfacesStr) {
					result.interfaces = interfacesStr
						.split(",")
						.map((i) => i.trim());
				}
			}
			continue;
		}

		// Parse methods and constructors
		const methodMatch = line.match(
			/^\s*((?:public |private |protected |static |final |synchronized |abstract )*)?(?:(\S+)\s+)?(\S+)\((.*?)\)(?:\s+throws\s+.+)?;\s*(?:\/\/\s*(.+))?$/
		);
		if (methodMatch) {
			const [, modifiersStr, returnType, name, paramsStr, descriptor] =
				methodMatch;

			const isConstructor = name === currentClassName;

			const method: JavaMethod = {
				name,
				kind: isConstructor ? "constructor" : "method",
				returnType: isConstructor
					? { kind: "class", name: currentClassName }
					: returnType
					? parseJavaType(returnType)
					: { kind: "primitive", type: "void" },
				modifiers: modifiersStr
					? (modifiersStr
							.trim()
							.split(" ")
							.filter(Boolean) as JavaModifier[])
					: [],
				parameters: parseParameters(paramsStr),
				descriptor: descriptor || "",
			};

			result.methods.push(method);
			continue;
		}

		// Parse fields
		const fieldMatch = line.match(
			/^\s*((?:public |private |protected |static |final |volatile |transient )*)?(\S+)\s+(\S+);(?:\s*\/\/\s*(.+))?$/
		);
		if (fieldMatch) {
			const [, modifiersStr, type, name, descriptor] = fieldMatch;
			const field: JavaField = {
				name,
				type: parseJavaType(type),
				modifiers: modifiersStr
					? (modifiersStr
							.trim()
							.split(" ")
							.filter(Boolean) as JavaModifier[])
					: [],
				descriptor: descriptor || "",
			};
			result.fields.push(field);
		}
	}

	return results;
}
