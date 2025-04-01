declare class sun_net_www_protocol_file_FileURLConnection extends sun_net_www_URLConnection {
	connect(): void;
	closeInputStream(): void;
	getHeaderField(arg_0: string): string;
	getHeaderField(arg_0: number): string;
	getContentLength(): number;
	getContentLengthLong(): number;
	getHeaderFieldKey(arg_0: number): string;
	getProperties(): sun_net_www_MessageHeader;
	getLastModified(): number;
	getInputStream(): java_io_InputStream;
	getPermission(): java_security_Permission
	[_BRAND]: {
		FileURLConnection: true
	};
}