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

/**
 * Represents a parameter in a Java method
 */
export type JavaMethodParameter = {
	/** The type of the parameter */
	type: JavaType;
	/** The name of the parameter, if available */
	name?: string;
	/** Indicates if this parameter is a varargs parameter (e.g., String... args) */
	isVarargs?: boolean;
};

export type JavaMethodKind = "constructor" | "method";

export type JavaMethod = {
	name: string;
	kind: JavaMethodKind;
	modifiers: JavaModifier[];
	returnType: JavaType;
	parameters: JavaMethodParameter[];
	descriptor: string;
	throws?: string[];
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
	isInterface?: boolean;
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

	// Handle generic types with commas inside them
	// We need to parse parameters more carefully to handle generics
	const params: JavaMethodParameter[] = [];
	let currentParam = "";
	let angleBracketDepth = 0;

	// Split parameters respecting generic type boundaries
	for (let i = 0; i < paramsStr.length; i++) {
		const char = paramsStr[i];

		if (char === "<") {
			angleBracketDepth++;
			currentParam += char;
		} else if (char === ">") {
			angleBracketDepth--;
			currentParam += char;
		} else if (char === "," && angleBracketDepth === 0) {
			// Only split on commas outside of generic types
			if (currentParam.trim()) {
				params.push(parseParameter(currentParam.trim()));
			}
			currentParam = "";
		} else {
			currentParam += char;
		}
	}

	// Add the last parameter
	if (currentParam.trim()) {
		params.push(parseParameter(currentParam.trim()));
	}

	return params;
}

function parseParameter(paramStr: string): JavaMethodParameter {
	// Check for varargs notation
	const isVarargs = paramStr.includes("...");

	// Remove the varargs ellipsis for further processing
	if (isVarargs) {
		paramStr = paramStr.replace("...", "");
	}

	// For complex generic types, we need to find the last space that's outside generic brackets
	// This separates the type from the parameter name
	let lastOutsideSpace = -1;
	let angleBracketDepth = 0;

	for (let i = 0; i < paramStr.length; i++) {
		const char = paramStr[i];

		if (char === "<") {
			angleBracketDepth++;
		} else if (char === ">") {
			angleBracketDepth--;
		} else if (char === " " && angleBracketDepth === 0) {
			lastOutsideSpace = i;
		}
	}

	// If we found a space outside generic brackets
	if (lastOutsideSpace !== -1) {
		const typeStr = paramStr.substring(0, lastOutsideSpace).trim();
		const nameStr = paramStr.substring(lastOutsideSpace + 1).trim();

		// Parse the type first
		let type = parseJavaType(typeStr);

		// For varargs, we need to convert the type to an array type if it's not already an array
		if (isVarargs && type.kind !== "array") {
			type = {
				kind: "array",
				type,
				dimensions: 1,
			};
		}

		// Create the parameter object with base properties
		const param: JavaMethodParameter = {
			type,
			name: nameStr,
		};

		// Only add isVarargs property if it's true
		if (isVarargs) {
			param.isVarargs = true;
		}

		return param;
	}

	// No space found, the whole string is the type
	let type = parseJavaType(paramStr.trim());

	// For varargs, we need to convert the type to an array type if it's not already an array
	if (isVarargs && type.kind !== "array") {
		type = {
			kind: "array",
			type,
			dimensions: 1,
		};
	}

	// Create the parameter object with base properties
	const param: JavaMethodParameter = {
		type,
	};

	// Only add isVarargs property if it's true
	if (isVarargs) {
		param.isVarargs = true;
	}

	return param;
}

/**
 * Helper function to parse interfaces from an implements clause
 */
function parseCommaSeparatedList(str: string): string[] {
	if (!str) return [];

	// Clean up the string and split by commas
	return str
		.trim()
		.split(/\s*,\s*/)
		.map((intf) => intf.trim())
		.filter(Boolean);
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
		isInterface: false,
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

		// Handle interface declaration
		const interfaceMatch = line.match(
			/^((?:public |private |protected |abstract )*)?interface\s+([^\s{]+)(?:\s+extends\s+([^{]+))?\s*\{?/
		);
		if (interfaceMatch) {
			const [, modifiersStr, fullInterfaceName, extendedInterfaces] =
				interfaceMatch;

			// Handle package and interface name
			const lastDotIndex = fullInterfaceName.lastIndexOf(".");
			if (lastDotIndex !== -1) {
				result.packageName = fullInterfaceName.substring(
					0,
					lastDotIndex
				);
				result.className = fullInterfaceName.substring(
					lastDotIndex + 1
				);
			} else {
				result.className = fullInterfaceName;
			}
			currentClassName = result.className;

			// Set isInterface flag
			result.isInterface = true;

			// Handle modifiers
			if (modifiersStr) {
				result.modifiers = modifiersStr
					.trim()
					.split(" ")
					.filter(Boolean) as JavaModifier[];
			}

			// Handle extended interfaces
			if (extendedInterfaces) {
				result.interfaces = parseCommaSeparatedList(extendedInterfaces);
			}
			continue;
		}

		// Handle class declaration - check this after interface to avoid mismatches
		if (line.match(/\bclass\b/)) {
			// First, try to extract the basic parts of the class declaration
			const classDeclarationParts = line.match(
				/^((?:public |private |protected |final |abstract )*)?class\s+([^\s<{]+)(?:<[^>]+>)?(.*)$/
			);

			if (classDeclarationParts) {
				const [, modifiersStr, fullClassName, remainingDeclaration] =
					classDeclarationParts;

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
				currentClassName = result.className;

				// Handle modifiers
				if (modifiersStr) {
					result.modifiers = modifiersStr
						.trim()
						.split(" ")
						.filter(Boolean) as JavaModifier[];
				}

				// Now, separately handle extends and implements clauses
				const extendsMatch = remainingDeclaration.match(
					/\s+extends\s+([^\s{]+(?:<[^>]+>)?)/
				);
				if (extendsMatch) {
					result.superClass = extendsMatch[1].trim();
				}

				const implementsMatch = remainingDeclaration.match(
					/\s+implements\s+([^{]+)/
				);
				if (implementsMatch) {
					const implementsStr = implementsMatch[1].trim();
					result.interfaces = parseCommaSeparatedList(implementsStr);
				}
			}
			continue;
		}

		// Parse methods and constructors
		// First, try to match constructor declarations (they don't have a return type)
		const constructorMatch = line.match(
			/^\s*((?:public |private |protected |static |final |synchronized |abstract )*)?(\S+)\((.*?)\)(?:\s+throws\s+(.+?))?;\s*(?:\/\/\s*(.+))?$/
		);

		if (constructorMatch) {
			const [, modifiersStr, fullName, paramsStr, throwsStr, descriptor] =
				constructorMatch;

			// Extract simple name from fully qualified name
			let name = fullName;
			const lastDotIndex = fullName.lastIndexOf(".");
			if (lastDotIndex !== -1) {
				name = fullName.substring(lastDotIndex + 1);
			}

			// Check if this is a constructor by comparing with the class name
			if (name === result.className) {
				const method: JavaMethod = {
					name,
					kind: "constructor",
					returnType: {
						kind: "class",
						name: result.packageName
							? `${result.packageName}.${result.className}`
							: result.className,
					},
					modifiers: modifiersStr
						? (modifiersStr
								.trim()
								.split(" ")
								.filter(Boolean) as JavaModifier[])
						: [],
					parameters: parseParameters(paramsStr),
					descriptor: descriptor || "",
				};

				// Parse throws clause if present
				if (throwsStr) {
					method.throws = parseCommaSeparatedList(throwsStr);
				}

				result.methods.push(method);
				continue;
			}
		}

		// Then try to match generic method declarations
		// First, try with complex generic type parameters with bounds
		const methodComplexGenericMatch = line.match(
			/^\s*((?:public |private |protected |static |final |synchronized |abstract )*)(?:<.*?extends.*?>\s+)(\S+(?:<[^>]+>)?)\s+(\S+)\((.*?)\)(?:\s+throws\s+(.+?))?;\s*(?:\/\/\s*(.+))?$/
		);

		// Then try simpler generic methods
		const methodGenericMatch = line.match(
			/^\s*((?:public |private |protected |static |final |synchronized |abstract )*)(?:<(.+?)>\s+)(\S+)\s+(\S+)\((.*?)\)(?:\s+throws\s+(.+?))?;\s*(?:\/\/\s*(.+))?$/
		);

		// And regular method declarations
		const methodMatch = line.match(
			/^\s*((?:public |private |protected |static |final |synchronized |abstract )*)(\S+(?:<[^>]+>)?)\s+(\S+)\((.*?)\)(?:\s+throws\s+(.+?))?;\s*(?:\/\/\s*(.+))?$/
		);

		if (methodComplexGenericMatch) {
			// Handle complex generic method declaration
			const [
				,
				modifiersStr,
				returnType,
				fullName,
				paramsStr,
				throwsStr,
				descriptor,
			] = methodComplexGenericMatch;

			// Extract simple name from fully qualified name
			let name = fullName;
			const lastDotIndex = fullName.lastIndexOf(".");
			if (lastDotIndex !== -1) {
				name = fullName.substring(lastDotIndex + 1);
			}

			const method: JavaMethod = {
				name,
				kind: "method",
				returnType: parseJavaType(returnType.trim()),
				modifiers: modifiersStr
					? (modifiersStr
							.trim()
							.split(" ")
							.filter(Boolean) as JavaModifier[])
					: [],
				parameters: parseParameters(paramsStr),
				descriptor: descriptor || "",
			};

			// Parse throws clause if present
			if (throwsStr) {
				method.throws = parseCommaSeparatedList(throwsStr);
			}

			result.methods.push(method);
			continue;
		} else if (methodGenericMatch) {
			// Handle generic method declaration
			const [
				,
				modifiersStr,
				,
				returnType,
				fullName,
				paramsStr,
				throwsStr,
				descriptor,
			] = methodGenericMatch;

			// Extract simple name from fully qualified name
			let name = fullName;
			const lastDotIndex = fullName.lastIndexOf(".");
			if (lastDotIndex !== -1) {
				name = fullName.substring(lastDotIndex + 1);
			}

			const method: JavaMethod = {
				name,
				kind: "method",
				returnType: parseJavaType(returnType.trim()),
				modifiers: modifiersStr
					? (modifiersStr
							.trim()
							.split(" ")
							.filter(Boolean) as JavaModifier[])
					: [],
				parameters: parseParameters(paramsStr),
				descriptor: descriptor || "",
			};

			// Parse throws clause if present
			if (throwsStr) {
				method.throws = parseCommaSeparatedList(throwsStr);
			}

			result.methods.push(method);
			continue;
		} else if (methodMatch) {
			// Handle regular method declaration
			const [
				,
				modifiersStr,
				returnType,
				fullName,
				paramsStr,
				throwsStr,
				descriptor,
			] = methodMatch;

			// Extract simple name from fully qualified name
			let name = fullName;
			const lastDotIndex = fullName.lastIndexOf(".");
			if (lastDotIndex !== -1) {
				name = fullName.substring(lastDotIndex + 1);
			}

			const method: JavaMethod = {
				name,
				kind: "method",
				returnType: parseJavaType(returnType.trim()),
				modifiers: modifiersStr
					? (modifiersStr
							.trim()
							.split(" ")
							.filter(Boolean) as JavaModifier[])
					: [],
				parameters: parseParameters(paramsStr),
				descriptor: descriptor || "",
			};

			// Parse throws clause if present
			if (throwsStr) {
				method.throws = parseCommaSeparatedList(throwsStr);
			}

			result.methods.push(method);
			continue;
		}

		// Parse fields - improved regex to handle both interface constants and class fields
		// This needs to come after method parsing to avoid mismatches with method declarations
		const fieldMatch = line.match(
			/^\s*((?:public |private |protected |static |final |volatile |transient )*)?(\S+)\s+(\S+)(?:\s*=\s*[^;]*)?;(?:\s*\/\/\s*(.+))?$/
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
			continue;
		}
	}

	return results;
}
