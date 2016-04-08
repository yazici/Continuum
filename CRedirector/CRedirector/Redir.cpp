//////////////////////////////////////////////////////////////////////
//
// Redirector - to redirect the input / output of a console
//
// Developer: Jeff Lee
// Dec 10, 2001
//
//////////////////////////////////////////////////////////////////////

#include "stdafx.h"
#include "Redir.h"
#include "afx.h"
#include <fstream>
using namespace std;

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif

#define _TEST_REDIR

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////

void writeLog(char *filename,char *logcontent){
	
	ofstream myfile;
	myfile.open (filename);

	myfile <<logcontent;

	myfile.close();
}

CRedirector::CRedirector() :
	m_hStdinWrite(NULL),
	m_hStdoutRead(NULL),
	m_hChildProcess(NULL),
	m_hThread(NULL),
	m_hEvtStop(NULL),
	m_dwThreadId(0),
	m_dwWaitTime(100)
{
}

CRedirector::~CRedirector()
{
	Close();
}

//////////////////////////////////////////////////////////////////////
// CRedirector implementation
//////////////////////////////////////////////////////////////////////

BOOL CRedirector::Open(LPCTSTR pszCmdLine)
{
	HANDLE hStdoutReadTmp;				// parent stdout read handle
	HANDLE hStdoutWrite, hStderrWrite;	// child stdout write handle
	HANDLE hStdinWriteTmp;				// parent stdin write handle
	HANDLE hStdinRead;					// child stdin read handle
	SECURITY_ATTRIBUTES sa;

	

	writeLog("CRedirector_open.txt","\t\nFlows into open function...\n");
	
	Close();
	hStdoutReadTmp = NULL;
	hStdoutWrite = hStderrWrite = NULL;
	hStdinWriteTmp = NULL;
	hStdinRead = NULL;

	// Set up the security attributes struct.
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;

	BOOL bOK = FALSE;
	__try
	{
		// Create a child stdout pipe.
		if (!::CreatePipe(&hStdoutReadTmp, &hStdoutWrite, &sa, 0))
			__leave;
		writeLog("CRedirector_open.txt","\tCreate a child stdout pipe\n");
		// Create a duplicate of the stdout write handle for the std
		// error write handle. This is necessary in case the child
		// application closes one of its std output handles.
		if (!::DuplicateHandle(
			::GetCurrentProcess(),
			hStdoutWrite,
			::GetCurrentProcess(),
			&hStderrWrite,
			0, TRUE,
			DUPLICATE_SAME_ACCESS))
			__leave;

		// Create a child stdin pipe.
		if (!::CreatePipe(&hStdinRead, &hStdinWriteTmp, &sa, 0))
			__leave;
		writeLog("CRedirector_open.txt","\tCreate a child stdin pipe\n");
		// Create new stdout read handle and the stdin write handle.
		// Set the inheritance properties to FALSE. Otherwise, the child
		// inherits the these handles; resulting in non-closeable
		// handles to the pipes being created.
		if (!::DuplicateHandle(
			::GetCurrentProcess(),
			hStdoutReadTmp,
			::GetCurrentProcess(),
			&m_hStdoutRead,
			0, FALSE,			// make it uninheritable.
			DUPLICATE_SAME_ACCESS))
			__leave;

		if (!::DuplicateHandle(
			::GetCurrentProcess(),
			hStdinWriteTmp,
			::GetCurrentProcess(),
			&m_hStdinWrite,
			0, FALSE,			// make it uninheritable.
			DUPLICATE_SAME_ACCESS))
			__leave;

		// Close inheritable copies of the handles we do not want to
		// be inherited.
		DestroyHandle(hStdoutReadTmp);
		DestroyHandle(hStdinWriteTmp);

		// launch the child process
		writeLog("CRedirector_open.txt","\tLaunching Child(the command line as input parameter of open)....\n");
		if (!LaunchChild(pszCmdLine,
			hStdoutWrite, hStdinRead, hStderrWrite))
			__leave;

		// Child is launched. Close the parents copy of those pipe
		// handles that only the child should have open.
		// Make sure that no handles to the write end of the stdout pipe
		// are maintained in this process or else the pipe will not
		// close when the child process exits and ReadFile will hang.
		DestroyHandle(hStdoutWrite);
		DestroyHandle(hStdinRead);
		DestroyHandle(hStderrWrite);

		// Launch a thread to receive output from the child process.
		writeLog("CRedirector_open.txt","\tLaunch a thread to receive output from the child process\n");
		m_hEvtStop = ::CreateEvent(NULL, TRUE, FALSE, NULL);
		m_hThread = ::CreateThread(
			NULL, 0,
			OutputThread,
			this,
			0,
			&m_dwThreadId);
		if (!m_hThread)
			{
				writeLog("CRedirector_open.txt","\t Error in creating Thread[in open function]\n");
				__leave;
			}

		bOK = TRUE;
			
	}

	__finally
	{
		if (!bOK)
		{
			DWORD dwOsErr = ::GetLastError();
			char szMsg[40];
			::sprintf(szMsg, "Redirect console error: %x\r\n", dwOsErr);
			WriteStdError(szMsg);
			DestroyHandle(hStdoutReadTmp);
			DestroyHandle(hStdoutWrite);
			DestroyHandle(hStderrWrite);
			DestroyHandle(hStdinWriteTmp);
			DestroyHandle(hStdinRead);
			Close();
			::SetLastError(dwOsErr);
		}
	}

	return bOK;
}

void CRedirector::Close()
{
	if (m_hThread != NULL)
	{
		// this function might be called from redir thread
		if (::GetCurrentThreadId() != m_dwThreadId)
		{
			ASSERT(m_hEvtStop != NULL);
			::SetEvent(m_hEvtStop);
			//::WaitForSingleObject(m_hThread, INFINITE);
			if (::WaitForSingleObject(m_hThread, 5000) == WAIT_TIMEOUT)
			{
				WriteStdError(_T("The redir thread is dead\r\n"));
				::TerminateThread(m_hThread, -2);
			}
		}

		DestroyHandle(m_hThread);
	}

	DestroyHandle(m_hEvtStop);
	DestroyHandle(m_hChildProcess);
	DestroyHandle(m_hStdinWrite);
	DestroyHandle(m_hStdoutRead);
	m_dwThreadId = 0;
}

// write data to the child's stdin
BOOL CRedirector::Printf(LPCTSTR pszFormat, ...)
{
	if (!m_hStdinWrite)
		return FALSE;
	ofstream myfile;
	myfile.open ("CRedirectorLog_Printf.txt");

	CString strInput;

	va_list argList;

	va_start(argList, pszFormat);
	strInput.FormatV(pszFormat, argList);
	va_end(argList);

	DWORD dwWritten;

	myfile <<"This data going to write into childs stdin :" << strInput <<"\n";
	myfile.close();

	return ::WriteFile(m_hStdinWrite, (LPCTSTR)strInput,
		strInput.GetLength(), &dwWritten, NULL);
}

BOOL CRedirector::LaunchChild(LPCTSTR pszCmdLine,
							  HANDLE hStdOut,
							  HANDLE hStdIn,
							  HANDLE hStdErr)
{
	PROCESS_INFORMATION pi;
	STARTUPINFO si;
	ofstream myfile;
	myfile.open ("CRedirectorLog_LaunchChild.txt");

	ASSERT(::AfxIsValidString(pszCmdLine));
	ASSERT(m_hChildProcess == NULL);

	// Set up the start up info struct.
	::ZeroMemory(&si, sizeof(STARTUPINFO));
	si.cb = sizeof(STARTUPINFO);
	si.hStdOutput = hStdOut;
	si.hStdInput = hStdIn;
	si.hStdError = hStdErr;
	si.wShowWindow = SW_HIDE;
	si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;

	// Note that dwFlags must include STARTF_USESHOWWINDOW if we
	// use the wShowWindow flags. This also assumes that the
	// CreateProcess() call will use CREATE_NEW_CONSOLE.

	myfile << "\tGoing to Launch the child process "<< pszCmdLine <<"\n";
	// Launch the child process.
	if (!::CreateProcess(
		NULL,
		(LPTSTR)pszCmdLine,
		NULL, NULL,
		TRUE,
		CREATE_NEW_CONSOLE,
		NULL, NULL,
		&si,
		&pi))
	{
		myfile << "\tCreate Child Process Failed\n";
		return FALSE;
	}

	m_hChildProcess = pi.hProcess;

	// Close any unuseful handles
	::CloseHandle(pi.hThread);

	myfile.close();

	return TRUE;
}

// redirect the child process's stdout:
// return: 1: no more data, 0: child terminated, -1: os error
int CRedirector::RedirectStdout()
{
	ofstream myfile;
	myfile.open ("CRedirectorLog_RedirectStdout.txt");

	myfile << "\tredirect the child process's stdout\n";

	ASSERT(m_hStdoutRead != NULL);
	for (;;)
	{
		DWORD dwAvail = 0;
		if (!::PeekNamedPipe(m_hStdoutRead, NULL, 0, NULL,
			&dwAvail, NULL))			// error
			break;

		if (!dwAvail)					// not data available
			return 1;

		char szOutput[256]; 
		DWORD dwRead = 0;
		if (!::ReadFile(m_hStdoutRead, szOutput, min(255, dwAvail),
			&dwRead, NULL) || !dwRead)	// error, the child might ended
			break;
		myfile << " \toutput buffer contents are : "<< szOutput <<"\n";
		szOutput[dwRead] = 0;
		WriteStdOut(szOutput);
	}

	DWORD dwError = ::GetLastError();
	if (dwError == ERROR_BROKEN_PIPE ||	// pipe has been ended
		dwError == ERROR_NO_DATA)		// pipe closing in progress
	{
#ifdef _TEST_REDIR
		WriteStdOut("\r\n<TEST INFO>: Child process ended\r\n");
#endif
		return 0;	// child process ended
	}

	WriteStdError("Read stdout pipe error\r\n");
	myfile.close();
	return -1;		// os error
}

void CRedirector::DestroyHandle(HANDLE& rhObject)
{
	if (rhObject != NULL)
	{
		::CloseHandle(rhObject);
		rhObject = NULL;
	}
}

void CRedirector::WriteStdOut(LPCSTR pszOutput)
{
	TRACE("%s", pszOutput);
}

void CRedirector::WriteStdError(LPCSTR pszError)
{
	TRACE("%s", pszError);
}

// thread to receive output of the child process
DWORD WINAPI CRedirector::OutputThread(LPVOID lpvThreadParam)
{
	ofstream myfile;
	myfile.open ("CRedirectorLog_OutputThread.txt");
	HANDLE aHandles[2];
	int nRet;
	CRedirector* pRedir = (CRedirector*) lpvThreadParam;

	myfile << "\tCRedirector object has created..\n";

	ASSERT(pRedir != NULL);
	aHandles[0] = pRedir->m_hChildProcess;
	aHandles[1] = pRedir->m_hEvtStop;

	for (;;)
	{
		// redirect stdout till there's no more data.
		nRet = pRedir->RedirectStdout();
		if (nRet <= 0)
			break;

		myfile << "\tWaiting for multiple objects..\n";
		// check if the child process has terminated.
		DWORD dwRc = ::WaitForMultipleObjects(
			2, aHandles, FALSE, pRedir->m_dwWaitTime);

		myfile <<"\tWait time we got is " << pRedir->m_dwWaitTime <<"\n";

		if (WAIT_OBJECT_0 == dwRc)		// the child process ended
		{
			myfile <<"\t child process terminated\n";
			nRet = pRedir->RedirectStdout();
			if (nRet > 0)
				nRet = 0;
			break;
		}
		if (WAIT_OBJECT_0+1 == dwRc)	// m_hEvtStop was signalled
		{
			myfile <<"\t Event Stop has signaled....\n";
			nRet = 1;	// cancelled
			break;
		}
	}
	myfile.close();
	// close handles
	pRedir->Close();
	return nRet;
}
